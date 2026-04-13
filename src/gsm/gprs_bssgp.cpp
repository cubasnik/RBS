#include "gprs_bssgp.h"
#include "../common/logger.h"

namespace rbs::gsm {

namespace {
// ── BSSGP IEI values (TS 48.018 Table 11.3-1) ───────────────────────────────
constexpr uint8_t IEI_BVCI         = 0x04;
constexpr uint8_t IEI_CAUSE        = 0x07;
constexpr uint8_t IEI_CELL_ID      = 0x08;
constexpr uint8_t IEI_LLC_PDU      = 0x0E;
constexpr uint8_t IEI_PDU_LIFETIME = 0x16;
constexpr uint8_t IEI_QOS_PROFILE  = 0x18;
constexpr uint8_t IEI_RADIO_CAUSE  = 0x19;
constexpr uint8_t IEI_TLLI         = 0x1F;
}  // namespace

GprsBssgp::GprsBssgp(uint16_t bvci) : bvci_(bvci) {}

// ── TLV helpers ──────────────────────────────────────────────────────────────

void GprsBssgp::appendTlv(ByteBuffer& buf, uint8_t iei,
                           const ByteBuffer& val) {
    buf.push_back(iei);
    buf.push_back(static_cast<uint8_t>(val.size()));
    buf.insert(buf.end(), val.begin(), val.end());
}

bool GprsBssgp::findTlv(const ByteBuffer& pdu, size_t start,
                         uint8_t iei, ByteBuffer& out) {
    for (size_t i = start; i + 1 < pdu.size(); ) {
        const uint8_t t = pdu[i];
        const uint8_t l = pdu[i + 1];
        if (i + 2u + l > pdu.size()) {
            break;
        }
        if (t == iei) {
            out.assign(pdu.begin() + static_cast<ptrdiff_t>(i + 2),
                       pdu.begin() + static_cast<ptrdiff_t>(i + 2 + l));
            return true;
        }
        i += 2u + l;
    }
    return false;
}

uint32_t GprsBssgp::decodeBe32(const ByteBuffer& v) {
    if (v.size() < 4) {
        return 0;
    }
    return (static_cast<uint32_t>(v[0]) << 24) |
           (static_cast<uint32_t>(v[1]) << 16) |
           (static_cast<uint32_t>(v[2]) <<  8) |
            static_cast<uint32_t>(v[3]);
}

uint16_t GprsBssgp::decodeBe16(const ByteBuffer& v) {
    if (v.size() < 2) {
        return 0;
    }
    return static_cast<uint16_t>((v[0] << 8) | v[1]);
}

// ── Cell ID BCD encoding (TS 48.018 §11.3.9 + TS 24.008 §10.5.1.3) ──────────
// Wire format (8 octets):
//   oct0: MCC digits 2+1 (upper/lower nibble)
//   oct1: MNC digit 3 (upper) | MCC digit 3 (lower)  — 0xF for 2-digit MNC
//   oct2: MNC digits 2+1 (upper/lower nibble)
//   oct3-4: LAC (big-endian)
//   oct5  : RAC
//   oct6-7: Cell Identity (big-endian)

void GprsBssgp::encodeCellId(ByteBuffer& buf, const BssgpCellId& c) {
    const uint8_t mcc1 = static_cast<uint8_t>(c.mcc % 10);
    const uint8_t mcc2 = static_cast<uint8_t>((c.mcc / 10) % 10);
    const uint8_t mcc3 = static_cast<uint8_t>((c.mcc / 100) % 10);
    const uint8_t mnc1 = static_cast<uint8_t>(c.mnc % 10);
    const uint8_t mnc2 = static_cast<uint8_t>((c.mnc / 10) % 10);
    const uint8_t mnc3 = (c.mnc >= 100)
                         ? static_cast<uint8_t>((c.mnc / 100) % 10)
                         : 0x0Fu;

    buf.push_back(static_cast<uint8_t>((mcc2 << 4) | mcc1));
    buf.push_back(static_cast<uint8_t>((mnc3 << 4) | mcc3));
    buf.push_back(static_cast<uint8_t>((mnc2 << 4) | mnc1));
    buf.push_back(static_cast<uint8_t>(c.lac >> 8));
    buf.push_back(static_cast<uint8_t>(c.lac & 0xFFu));
    buf.push_back(c.rac);
    buf.push_back(static_cast<uint8_t>(c.ci >> 8));
    buf.push_back(static_cast<uint8_t>(c.ci & 0xFFu));
}

bool GprsBssgp::decodeCellId(const ByteBuffer& data, BssgpCellId& cell) {
    if (data.size() < 8) {
        return false;
    }
    const uint8_t mcc1 =  data[0]        & 0x0Fu;
    const uint8_t mcc2 = (data[0] >> 4)  & 0x0Fu;
    const uint8_t mcc3 =  data[1]        & 0x0Fu;
    const uint8_t mnc3 = (data[1] >> 4)  & 0x0Fu;
    const uint8_t mnc1 =  data[2]        & 0x0Fu;
    const uint8_t mnc2 = (data[2] >> 4)  & 0x0Fu;
    cell.mcc = static_cast<uint16_t>(mcc1 + mcc2 * 10u + mcc3 * 100u);
    cell.mnc = (mnc3 == 0x0Fu)
               ? static_cast<uint16_t>(mnc1 + mnc2 * 10u)
               : static_cast<uint16_t>(mnc1 + mnc2 * 10u + mnc3 * 100u);
    cell.lac = static_cast<uint16_t>((data[3] << 8) | data[4]);
    cell.rac = data[5];
    cell.ci  = static_cast<uint16_t>((data[6] << 8) | data[7]);
    return true;
}

ByteBuffer GprsBssgp::encodeQoS(const BssgpQoS& qos) {
    // 3-byte profile per TS 48.018 §11.3.28 (simplified):
    // octet 0: delay class [6:4] | reliability class [2:0]
    // octet 1: precedence class [6:4] | peak throughput [3:0]
    // octet 2: reserved
    return {
        static_cast<uint8_t>(((qos.delayClass & 0x7u) << 4) |
                              (qos.reliabilityClass & 0x7u)),
        static_cast<uint8_t>(((qos.precedenceClass & 0x7u) << 4) |
                              (qos.peakThroughput & 0xFu)),
        0x00u
    };
}

// ── Encode ───────────────────────────────────────────────────────────────────

ByteBuffer GprsBssgp::encodeUlUnitdata(uint32_t tlli,
                                       const BssgpQoS& qos,
                                       const BssgpCellId& cellId,
                                       const ByteBuffer& llcPdu) const {
    ByteBuffer pdu;
    pdu.push_back(static_cast<uint8_t>(BssgpPduType::UL_UNITDATA));
    appendTlv(pdu, IEI_TLLI, {
        static_cast<uint8_t>(tlli >> 24), static_cast<uint8_t>(tlli >> 16),
        static_cast<uint8_t>(tlli >>  8), static_cast<uint8_t>(tlli)
    });
    appendTlv(pdu, IEI_QOS_PROFILE, encodeQoS(qos));
    ByteBuffer cellBuf;
    encodeCellId(cellBuf, cellId);
    appendTlv(pdu, IEI_CELL_ID, cellBuf);
    appendTlv(pdu, IEI_LLC_PDU, llcPdu);
    return pdu;
}

ByteBuffer GprsBssgp::encodeDlUnitdata(uint32_t tlli,
                                       const BssgpQoS& qos,
                                       uint16_t pduLifetimeCs,
                                       const ByteBuffer& llcPdu) const {
    ByteBuffer pdu;
    pdu.push_back(static_cast<uint8_t>(BssgpPduType::DL_UNITDATA));
    appendTlv(pdu, IEI_TLLI, {
        static_cast<uint8_t>(tlli >> 24), static_cast<uint8_t>(tlli >> 16),
        static_cast<uint8_t>(tlli >>  8), static_cast<uint8_t>(tlli)
    });
    appendTlv(pdu, IEI_QOS_PROFILE, encodeQoS(qos));
    appendTlv(pdu, IEI_PDU_LIFETIME, {
        static_cast<uint8_t>(pduLifetimeCs >> 8),
        static_cast<uint8_t>(pduLifetimeCs)
    });
    appendTlv(pdu, IEI_LLC_PDU, llcPdu);
    return pdu;
}

ByteBuffer GprsBssgp::encodeBvcReset(BssgpCause cause) const {
    ByteBuffer pdu;
    pdu.push_back(static_cast<uint8_t>(BssgpPduType::BVC_RESET));
    appendTlv(pdu, IEI_BVCI, {static_cast<uint8_t>(bvci_ >> 8),
                               static_cast<uint8_t>(bvci_)});
    appendTlv(pdu, IEI_CAUSE, {static_cast<uint8_t>(cause)});
    return pdu;
}

ByteBuffer GprsBssgp::encodeBvcResetAck() const {
    ByteBuffer pdu;
    pdu.push_back(static_cast<uint8_t>(BssgpPduType::BVC_RESET_ACK));
    appendTlv(pdu, IEI_BVCI, {static_cast<uint8_t>(bvci_ >> 8),
                               static_cast<uint8_t>(bvci_)});
    return pdu;
}

ByteBuffer GprsBssgp::encodeRadioStatus(uint32_t tlli,
                                        BssgpCause cause) const {
    ByteBuffer pdu;
    pdu.push_back(static_cast<uint8_t>(BssgpPduType::RADIO_STATUS));
    appendTlv(pdu, IEI_TLLI, {
        static_cast<uint8_t>(tlli >> 24), static_cast<uint8_t>(tlli >> 16),
        static_cast<uint8_t>(tlli >>  8), static_cast<uint8_t>(tlli)
    });
    appendTlv(pdu, IEI_RADIO_CAUSE, {static_cast<uint8_t>(cause)});
    return pdu;
}

// ── Decode / handle ──────────────────────────────────────────────────────────

ByteBuffer GprsBssgp::handlePdu(const ByteBuffer& pdu,
                                GprsBssgpTrace& trace,
                                ByteBuffer& response) {
    response.clear();
    if (pdu.empty()) {
        return {};
    }
    const auto type = static_cast<BssgpPduType>(pdu[0]);

    switch (type) {
    case BssgpPduType::UL_UNITDATA: {
        ByteBuffer tlliBytes, llcBytes, cellIdBytes;
        findTlv(pdu, 1, IEI_TLLI,    tlliBytes);
        findTlv(pdu, 1, IEI_LLC_PDU, llcBytes);
        BssgpCellId cell{};
        if (findTlv(pdu, 1, IEI_CELL_ID, cellIdBytes)) {
            decodeCellId(cellIdBytes, cell);
        }
        trace.dir      = GprsBssgpTrace::Dir::UL;
        trace.tlli     = decodeBe32(tlliBytes);
        trace.bvci     = bvci_;
        trace.llcBytes = static_cast<uint32_t>(llcBytes.size());
        trace.cellId   = cell;
        traceLog_.push_back(trace);
        RBS_LOG_DEBUG("BSSGP", "UL-UNITDATA TLLI={} bytes={}", trace.tlli, trace.llcBytes);
        return llcBytes;
    }

    case BssgpPduType::DL_UNITDATA: {
        ByteBuffer tlliBytes, llcBytes;
        findTlv(pdu, 1, IEI_TLLI,    tlliBytes);
        findTlv(pdu, 1, IEI_LLC_PDU, llcBytes);
        trace.dir      = GprsBssgpTrace::Dir::DL;
        trace.tlli     = decodeBe32(tlliBytes);
        trace.bvci     = bvci_;
        trace.llcBytes = static_cast<uint32_t>(llcBytes.size());
        traceLog_.push_back(trace);
        RBS_LOG_DEBUG("BSSGP", "DL-UNITDATA TLLI={} bytes={}", trace.tlli, trace.llcBytes);
        return llcBytes;
    }

    case BssgpPduType::BVC_RESET:
        RBS_LOG_INFO("BSSGP", "BVC-RESET rx BVCI={}", bvci_);
        response = encodeBvcResetAck();
        break;

    case BssgpPduType::BVC_RESET_ACK:
        RBS_LOG_INFO("BSSGP", "BVC-RESET-ACK rx BVCI={}", bvci_);
        break;

    case BssgpPduType::RADIO_STATUS: {
        ByteBuffer tlliBytes;
        findTlv(pdu, 1, IEI_TLLI, tlliBytes);
        RBS_LOG_INFO("BSSGP", "RADIO-STATUS TLLI={} BVCI={}", decodeBe32(tlliBytes), bvci_);
        break;
    }

    default:
        RBS_LOG_DEBUG("BSSGP", "Unhandled PDU type={}", static_cast<unsigned>(pdu[0]));
        break;
    }

    return {};
}

}  // namespace rbs::gsm
