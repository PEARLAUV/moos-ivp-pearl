#ifndef MAVLINK_TCP_HEADER
#define MAVLINK_TCP_HEADER

#include <cstdint>
#include <string>
#include <vector>
#include <memory>

namespace mavlink
{
using Payload = std::vector<uint8_t>;
constexpr uint32_t HEARTBEAT = 0;
constexpr uint32_t GLOBAL_POSITION_INT = 33;
constexpr uint32_t COMMAND_LONG = 76;
struct Frame;
class Decoder;
constexpr uint32_t LANDING_TARGET = 149;

void appendUint8(Payload&, uint8_t);
void appendUint16(Payload&, uint16_t);
void appendInt16(Payload&, int16_t);
void appendUint32(Payload&, uint32_t);
void appendInt32(Payload&, int32_t);
void appendUint64(Payload&, uint64_t);
void appendFloat(Payload&, float);
Payload heartbeat(uint8_t vehicle_type);
double wrapHeading(double);
uint64_t unixMicros();
uint32_t steadyMillis();

// One nonblocking TCP session per producer. No MOOS state or vehicle policy.
class TcpClient
{
public:
  ~TcpClient();
  TcpClient();
  TcpClient(const TcpClient&) = delete;
  TcpClient& operator=(const TcpClient&) = delete;
  bool connectTo(const std::string& host, unsigned int port);
  void close();
  bool connected() const { return m_fd >= 0; }
  void drain();
  std::vector<Frame> receive();
  bool send(uint32_t message_id, const Payload&, uint8_t system_id,
            uint8_t component_id);
private:
  int m_fd = -1;
  uint8_t m_sequence = 0;
  std::unique_ptr<Decoder> m_decoder;
};
} // namespace mavlink
#endif
