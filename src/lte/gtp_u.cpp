#include "gtp_u.h"
#include "../common/logger.h"

namespace rbs::lte {

ByteBuffer gtpuEncode(uint32_t teid, const ByteBuffer& payload)
{
    uint16_t length = static_cast<uint16_t>(payload.size());
    ByteBuffer out;
    out.reserve(GTPU_HEADER_SIZE + payload.size());

    // Заголовок (big-endian)
    out.push_back(GTPU_FLAGS_MINIMAL);
    out.push_back(GTPU_MSG_TPDU);
    out.push_back(static_cast<uint8_t>(length >> 8));
    out.push_back(static_cast<uint8_t>(length));
    out.push_back(static_cast<uint8_t>(teid >> 24));
    out.push_back(static_cast<uint8_t>(teid >> 16));
    out.push_back(static_cast<uint8_t>(teid >> 8));
    out.push_back(static_cast<uint8_t>(teid));

    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

bool gtpuDecode(const ByteBuffer& raw, uint32_t& teid, ByteBuffer& payload)
{
    if (raw.size() < GTPU_HEADER_SIZE) {
        RBS_LOG_WARNING("GTP-U", "слишком короткий пакет: {} байт", raw.size());
        return false;
    }
    if (raw[0] != GTPU_FLAGS_MINIMAL) {
        RBS_LOG_WARNING("GTP-U", "неверный flags=0x{:02X} (ожидается 0x{:02X})",
                         raw[0], GTPU_FLAGS_MINIMAL);
        return false;
    }
    if (raw[1] != GTPU_MSG_TPDU) {
        RBS_LOG_WARNING("GTP-U", "неверный msgType=0x{:02X} (только T-PDU поддерживается)",
                         raw[1]);
        return false;
    }
    uint16_t declaredLen = static_cast<uint16_t>((raw[2] << 8) | raw[3]);
    if (raw.size() < GTPU_HEADER_SIZE + declaredLen) {
        RBS_LOG_WARNING("GTP-U", "усечённый пакет: есть {} байт данных, заявлено {}",
                         raw.size() - GTPU_HEADER_SIZE, declaredLen);
        return false;
    }

    teid = (static_cast<uint32_t>(raw[4]) << 24) |
           (static_cast<uint32_t>(raw[5]) << 16) |
           (static_cast<uint32_t>(raw[6]) << 8)  |
            static_cast<uint32_t>(raw[7]);

    payload.assign(raw.begin() + GTPU_HEADER_SIZE,
                   raw.begin() + GTPU_HEADER_SIZE + declaredLen);
    return true;
}

} // namespace rbs::lte
