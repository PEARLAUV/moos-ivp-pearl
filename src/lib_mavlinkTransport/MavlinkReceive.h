#ifndef MAVLINK_RECEIVE_HEADER
#define MAVLINK_RECEIVE_HEADER
#include "MavlinkTcp.h"
#include <algorithm>
namespace mavlink
{
struct Frame
{
  uint32_t id = 0;
  uint8_t system = 0, component = 0;
  Payload payload;
};
inline uint8_t receiveExtra(uint32_t id)
{
  switch (id) {
  case 0:
    return 50;
  case 30:
    return 39;
  case 31:
    return 246;
  case 33:
    return 104;
  case 77:
    return 143;
  default:
    return 0;
  }
}
// Incremental v1/v2 decoder. CRC checked; MAVLink signatures are not authenticated.
class Decoder
{
public:
  void clear()
  {
    m_buffer.clear();
  }
  std::vector<Frame> feed(const uint8_t *data, size_t count)
  {
    m_buffer.insert(m_buffer.end(), data, data + count);
    std::vector<Frame> frames;
    while (m_buffer.size() >= 8) {
      const bool v2 = m_buffer[0] == 0xfd;
      if (!v2 && m_buffer[0] != 0xfe) {
        m_buffer.erase(m_buffer.begin());
        continue;
      }
      const size_t header = v2 ? 10 : 6;
      if (m_buffer.size() < header)
        break;
      if (v2 && (m_buffer[2] & ~1U)) {
        m_buffer.erase(m_buffer.begin());
        continue;
      }
      const size_t length = m_buffer[1], crc_index = header + length;
      const size_t total = crc_index + 2 + (v2 && (m_buffer[2] & 1) ? 13 : 0);
      if (m_buffer.size() < total)
        break;
      const uint32_t id =
          v2 ? (m_buffer[7] | (uint32_t(m_buffer[8]) << 8) | (uint32_t(m_buffer[9]) << 16))
             : m_buffer[5];
      const uint8_t extra = receiveExtra(id);
      if (extra) {
        uint16_t crc = 0xffff;
        for (size_t i = 1; i <= crc_index; ++i) {
          const uint8_t byte = i == crc_index ? extra : m_buffer[i];
          uint8_t tmp = byte ^ uint8_t(crc & 255);
          tmp ^= uint8_t(tmp << 4);
          crc = uint16_t((crc >> 8) ^ (uint16_t(tmp) << 8) ^ (uint16_t(tmp) << 3) ^ (tmp >> 4));
        }
        if (crc != (m_buffer[crc_index] | (uint16_t(m_buffer[crc_index + 1]) << 8))) {
          m_buffer.erase(m_buffer.begin());
          continue;
        }
        Frame frame;
        frame.id = id;
        frame.system = m_buffer[v2 ? 5 : 3];
        frame.component = m_buffer[v2 ? 6 : 4];
        frame.payload.assign(m_buffer.begin() + header, m_buffer.begin() + crc_index);
        frames.push_back(frame);
      }
      m_buffer.erase(m_buffer.begin(), m_buffer.begin() + total);
    }
    return frames;
  }

private:
  Payload m_buffer;
};
} // namespace mavlink
#endif
