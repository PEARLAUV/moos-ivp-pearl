#include "MavlinkTcp.h"
#include "MavlinkReceive.h"
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace mavlink
{
namespace {
uint8_t crcExtra(uint32_t message_id)
{
  switch(message_id) {
  case HEARTBEAT:
    return 50;
  case GLOBAL_POSITION_INT:
    return 104;
  case LANDING_TARGET:
    return 200;
  case COMMAND_LONG:
    return 152;
  default:
    return 0;
  }
}

uint16_t computeChecksum(const std::vector<uint8_t> &data)
{
  uint16_t crc = 0xFFFF;
  for(const uint8_t byte : data) {
    uint8_t tmp = byte ^ static_cast<uint8_t>(crc & 0xFF);
    tmp ^= static_cast<uint8_t>(tmp << 4);
    crc = static_cast<uint16_t>((crc >> 8) ^ (static_cast<uint16_t>(tmp) << 8) ^
                                (static_cast<uint16_t>(tmp) << 3) ^ (tmp >> 4));
  }
  return crc;
}

} // namespace

double wrapHeading(double heading)
{
  heading = std::fmod(heading, 360.0);
  return heading < 0 ? heading + 360.0 : heading;
}

uint64_t unixMicros()
{
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  return std::chrono::duration_cast<std::chrono::microseconds>(now).count();
}

uint32_t steadyMillis()
{
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  return static_cast<uint32_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

void appendUint8(std::vector<uint8_t> &payload, uint8_t value)
{
  payload.push_back(value);
}

void appendUint16(std::vector<uint8_t> &payload, uint16_t value)
{
  payload.push_back(static_cast<uint8_t>(value & 0xFF));
  payload.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
}

void appendInt16(std::vector<uint8_t> &payload, int16_t value)
{
  appendUint16(payload, static_cast<uint16_t>(value));
}

void appendUint32(std::vector<uint8_t> &payload, uint32_t value)
{
  for(unsigned int shift = 0; shift < 32; shift += 8)
    payload.push_back(static_cast<uint8_t>((value >> shift) & 0xFF));
}

void appendInt32(std::vector<uint8_t> &payload, int32_t value)
{
  appendUint32(payload, static_cast<uint32_t>(value));
}

void appendUint64(std::vector<uint8_t> &payload, uint64_t value)
{
  for(unsigned int shift = 0; shift < 64; shift += 8)
    payload.push_back(static_cast<uint8_t>((value >> shift) & 0xFF));
}

void appendFloat(std::vector<uint8_t> &payload, float value)
{
  uint32_t bits = 0;
  static_assert(sizeof(bits) == sizeof(value), "Unexpected float size");
  std::memcpy(&bits, &value, sizeof(bits));
  appendUint32(payload, bits);
}

Payload heartbeat(uint8_t vehicle_type)
{
  Payload payload;
  appendUint32(payload, 0);
  appendUint8(payload, vehicle_type);
  appendUint8(payload, 8); // MAV_AUTOPILOT_INVALID
  appendUint8(payload, 0);
  appendUint8(payload, 4); // MAV_STATE_ACTIVE
  appendUint8(payload, 3);
  return payload;
}

TcpClient::TcpClient() : m_decoder(new Decoder) {}
TcpClient::~TcpClient() { close(); }

bool TcpClient::connectTo(const std::string& host, unsigned int endpoint_port)
{
  close();
  struct addrinfo hints {};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  struct addrinfo *addresses = nullptr;
  const std::string port = std::to_string(endpoint_port);
  if(getaddrinfo(host.c_str(), port.c_str(), &hints, &addresses) != 0)
    return false;

  int connected_socket = -1;
  for(struct addrinfo *address = addresses; address != nullptr;
      address = address->ai_next) {
    connected_socket = socket(address->ai_family, address->ai_socktype,
                              address->ai_protocol);
    if(connected_socket < 0)
      continue;
    const int flags = fcntl(connected_socket, F_GETFL, 0);
    if(flags >= 0 && fcntl(connected_socket, F_SETFL, flags | O_NONBLOCK) == 0) {
      int result = connect(connected_socket, address->ai_addr, address->ai_addrlen);
      if(result < 0 && errno == EINPROGRESS) {
        struct pollfd poll_socket { connected_socket, POLLOUT, 0 };
        if(poll(&poll_socket, 1, 100) > 0) {
          int socket_error = 0;
          socklen_t size = sizeof(socket_error);
          if(getsockopt(connected_socket, SOL_SOCKET, SO_ERROR,
                        &socket_error, &size) == 0 && socket_error == 0)
            result = 0;
        }
      }
      if(result == 0)
        break;
    }
    ::close(connected_socket);
    connected_socket = -1;
  }
  freeaddrinfo(addresses);

  if(connected_socket < 0)
    return false;

  m_fd = connected_socket;
  return true;
}

void TcpClient::close()
{
  if(m_fd >= 0)
    ::close(m_fd);
  m_fd = -1;
  m_decoder->clear();
}

std::vector<Frame> TcpClient::receive()
{
  std::vector<Frame> frames;
  uint8_t buffer[4096];
  // Bounded work even if a peer streams continuously. Partial frames persist.
  for (unsigned reads = 0; m_fd >= 0 && reads < 16; ++reads) {
    const ssize_t count = recv(m_fd, buffer, sizeof(buffer), MSG_DONTWAIT);
    if (count > 0) {
      const auto parsed = m_decoder->feed(buffer, static_cast<size_t>(count));
      frames.insert(frames.end(), parsed.begin(), parsed.end());
    } else if (count == 0) {
      close(); break;
    } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
      break;
    } else if (errno != EINTR) {
      close(); break;
    }
  }
  return frames;
}

void TcpClient::drain()
{
  uint8_t buffer[4096];
  while(m_fd >= 0) {
    const ssize_t received = recv(m_fd, buffer, sizeof(buffer), MSG_DONTWAIT);
    if(received > 0)
      continue;
    if(received == 0) {
      close();
      return;
    }
    if(errno == EAGAIN || errno == EWOULDBLOCK)
      return;
    if(errno == EINTR)
      continue;
    close();
    return;
  }
}

bool TcpClient::send(uint32_t message_id, const Payload &payload,
                     uint8_t system_id, uint8_t component_id)
{
  if(m_fd < 0 || payload.size() > 255 || crcExtra(message_id) == 0)
    return false;

  std::vector<uint8_t> header;
  header.reserve(9);
  appendUint8(header, static_cast<uint8_t>(payload.size()));
  appendUint8(header, 0);
  appendUint8(header, 0);
  appendUint8(header, m_sequence++);
  appendUint8(header, system_id);
  appendUint8(header, component_id);
  appendUint8(header, static_cast<uint8_t>(message_id & 0xFF));
  appendUint8(header, static_cast<uint8_t>((message_id >> 8) & 0xFF));
  appendUint8(header, static_cast<uint8_t>((message_id >> 16) & 0xFF));

  std::vector<uint8_t> checksum_data = header;
  checksum_data.insert(checksum_data.end(), payload.begin(), payload.end());
  appendUint8(checksum_data, crcExtra(message_id));
  const uint16_t checksum = computeChecksum(checksum_data);

  std::vector<uint8_t> packet;
  packet.reserve(1 + header.size() + payload.size() + 2);
  appendUint8(packet, 0xFD);
  packet.insert(packet.end(), header.begin(), header.end());
  packet.insert(packet.end(), payload.begin(), payload.end());
  appendUint16(packet, checksum);

  size_t sent_total = 0;
  while(sent_total < packet.size()) {
    const ssize_t sent = ::send(m_fd, packet.data() + sent_total,
                              packet.size() - sent_total, MSG_NOSIGNAL);
    if(sent > 0) {
      sent_total += static_cast<size_t>(sent);
      continue;
    }
    if(sent < 0 && errno == EINTR)
      continue;
    close();
    return false;
  }
  return true;
}

} // namespace mavlink
