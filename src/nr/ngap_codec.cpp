#include "ngap_codec.h"

namespace rbs::nr {

namespace {

constexpr uint8_t NGAP_MAGIC0 = 0x4E;
constexpr uint8_t NGAP_MAGIC1 = 0x47;

void pushU8(ByteBuffer& buffer, uint8_t value) { buffer.push_back(value); }

void pushU16(ByteBuffer& buffer, uint16_t value) {
    buffer.push_back(static_cast<uint8_t>(value >> 8));
    buffer.push_back(static_cast<uint8_t>(value));
}

void pushU32(ByteBuffer& buffer, uint32_t value) {
    buffer.push_back(static_cast<uint8_t>(value >> 24));
    buffer.push_back(static_cast<uint8_t>(value >> 16));
    buffer.push_back(static_cast<uint8_t>(value >> 8));
    buffer.push_back(static_cast<uint8_t>(value));
}

void pushU64(ByteBuffer& buffer, uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        buffer.push_back(static_cast<uint8_t>(value >> shift));
    }
}

void pushStr(ByteBuffer& buffer, const std::string& value) {
    const uint16_t size = static_cast<uint16_t>(value.size() > 0xFFFF ? 0xFFFF : value.size());
    pushU16(buffer, size);
    buffer.insert(buffer.end(), value.begin(), value.begin() + size);
}

void pushBytes(ByteBuffer& buffer, const ByteBuffer& value) {
    const uint16_t size = static_cast<uint16_t>(value.size() > 0xFFFF ? 0xFFFF : value.size());
    pushU16(buffer, size);
    buffer.insert(buffer.end(), value.begin(), value.begin() + size);
}

struct Reader {
    const ByteBuffer& buffer;
    size_t position = 0;
    bool ok = true;

    uint8_t u8() {
        if (position + 1 > buffer.size()) {
            ok = false;
            return 0;
        }
        return buffer[position++];
    }

    uint16_t u16() {
        if (position + 2 > buffer.size()) {
            ok = false;
            return 0;
        }
        const uint16_t value = static_cast<uint16_t>(buffer[position] << 8) | buffer[position + 1];
        position += 2;
        return value;
    }

    uint32_t u32() {
        if (position + 4 > buffer.size()) {
            ok = false;
            return 0;
        }
        const uint32_t value = (static_cast<uint32_t>(buffer[position]) << 24)
                             | (static_cast<uint32_t>(buffer[position + 1]) << 16)
                             | (static_cast<uint32_t>(buffer[position + 2]) << 8)
                             | static_cast<uint32_t>(buffer[position + 3]);
        position += 4;
        return value;
    }

    uint64_t u64() {
        if (position + 8 > buffer.size()) {
            ok = false;
            return 0;
        }
        uint64_t value = 0;
        for (int i = 0; i < 8; ++i) {
            value = (value << 8) | buffer[position++];
        }
        return value;
    }

    std::string str() {
        const uint16_t size = u16();
        if (!ok || position + size > buffer.size()) {
            ok = false;
            return {};
        }
        std::string value(reinterpret_cast<const char*>(&buffer[position]), size);
        position += size;
        return value;
    }

    ByteBuffer bytes() {
        const uint16_t size = u16();
        if (!ok || position + size > buffer.size()) {
            ok = false;
            return {};
        }
        ByteBuffer value(buffer.begin() + static_cast<std::ptrdiff_t>(position),
                         buffer.begin() + static_cast<std::ptrdiff_t>(position + size));
        position += size;
        return value;
    }
};

void pushHeader(ByteBuffer& buffer, NgapProcedure procedure) {
    pushU8(buffer, NGAP_MAGIC0);
    pushU8(buffer, NGAP_MAGIC1);
    pushU8(buffer, static_cast<uint8_t>(procedure));
}

bool checkHeader(Reader& reader, NgapProcedure procedure) {
    return reader.u8() == NGAP_MAGIC0
        && reader.u8() == NGAP_MAGIC1
        && reader.u8() == static_cast<uint8_t>(procedure)
        && reader.ok;
}

}  // namespace

ByteBuffer encodeNgSetupRequest(const NgSetupRequest& req) {
    ByteBuffer buffer;
    pushHeader(buffer, NgapProcedure::NG_SETUP_REQUEST);
    pushU16(buffer, req.transactionId);
    pushU64(buffer, req.ranNodeId);
    pushStr(buffer, req.gnbName);
    pushU16(buffer, req.tac);
    pushU16(buffer, req.mcc);
    pushU16(buffer, req.mnc);
    return buffer;
}

bool decodeNgSetupRequest(const ByteBuffer& pdu, NgSetupRequest& out) {
    Reader reader{pdu};
    if (!checkHeader(reader, NgapProcedure::NG_SETUP_REQUEST)) {
        return false;
    }
    out.transactionId = reader.u16();
    out.ranNodeId = reader.u64();
    out.gnbName = reader.str();
    out.tac = reader.u16();
    out.mcc = reader.u16();
    out.mnc = reader.u16();
    return reader.ok;
}

ByteBuffer encodeNgSetupResponse(const NgSetupResponse& rsp) {
    ByteBuffer buffer;
    pushHeader(buffer, NgapProcedure::NG_SETUP_RESPONSE);
    pushU16(buffer, rsp.transactionId);
    pushU64(buffer, rsp.amfId);
    pushStr(buffer, rsp.amfName);
    pushU16(buffer, rsp.relativeCapacity);
    return buffer;
}

bool decodeNgSetupResponse(const ByteBuffer& pdu, NgSetupResponse& out) {
    Reader reader{pdu};
    if (!checkHeader(reader, NgapProcedure::NG_SETUP_RESPONSE)) {
        return false;
    }
    out.transactionId = reader.u16();
    out.amfId = reader.u64();
    out.amfName = reader.str();
    out.relativeCapacity = reader.u16();
    return reader.ok;
}

ByteBuffer encodePduSessionSetupRequest(const PduSessionSetupRequest& req) {
    ByteBuffer buffer;
    pushHeader(buffer, NgapProcedure::PDU_SESSION_SETUP_REQUEST);
    pushU16(buffer, req.transactionId);
    pushU64(buffer, req.amfUeNgapId);
    pushU16(buffer, req.ranUeNgapId);
    pushU8(buffer, req.pduSessionId);
    pushU8(buffer, req.sst);
    pushU32(buffer, req.sd);
    pushBytes(buffer, req.nasPdu);
    return buffer;
}

bool decodePduSessionSetupRequest(const ByteBuffer& pdu, PduSessionSetupRequest& out) {
    Reader reader{pdu};
    if (!checkHeader(reader, NgapProcedure::PDU_SESSION_SETUP_REQUEST)) {
        return false;
    }
    out.transactionId = reader.u16();
    out.amfUeNgapId = reader.u64();
    out.ranUeNgapId = reader.u16();
    out.pduSessionId = reader.u8();
    out.sst = reader.u8();
    out.sd = reader.u32();
    out.nasPdu = reader.bytes();
    return reader.ok;
}

ByteBuffer encodePduSessionSetupResponse(const PduSessionSetupResponse& rsp) {
    ByteBuffer buffer;
    pushHeader(buffer, NgapProcedure::PDU_SESSION_SETUP_RESPONSE);
    pushU16(buffer, rsp.transactionId);
    pushU64(buffer, rsp.amfUeNgapId);
    pushU16(buffer, rsp.ranUeNgapId);
    pushU8(buffer, rsp.pduSessionId);
    pushU32(buffer, rsp.gtpTeid);
    pushBytes(buffer, rsp.transfer);
    return buffer;
}

bool decodePduSessionSetupResponse(const ByteBuffer& pdu, PduSessionSetupResponse& out) {
    Reader reader{pdu};
    if (!checkHeader(reader, NgapProcedure::PDU_SESSION_SETUP_RESPONSE)) {
        return false;
    }
    out.transactionId = reader.u16();
    out.amfUeNgapId = reader.u64();
    out.ranUeNgapId = reader.u16();
    out.pduSessionId = reader.u8();
    out.gtpTeid = reader.u32();
    out.transfer = reader.bytes();
    return reader.ok;
}

ByteBuffer encodeUeContextReleaseCommand(const UeContextReleaseCommand& cmd) {
    ByteBuffer buffer;
    pushHeader(buffer, NgapProcedure::UE_CONTEXT_RELEASE_COMMAND);
    pushU16(buffer, cmd.transactionId);
    pushU64(buffer, cmd.amfUeNgapId);
    pushU16(buffer, cmd.ranUeNgapId);
    pushU8(buffer, cmd.causeType);
    pushU8(buffer, cmd.causeValue);
    return buffer;
}

bool decodeUeContextReleaseCommand(const ByteBuffer& pdu, UeContextReleaseCommand& out) {
    Reader reader{pdu};
    if (!checkHeader(reader, NgapProcedure::UE_CONTEXT_RELEASE_COMMAND)) {
        return false;
    }
    out.transactionId = reader.u16();
    out.amfUeNgapId = reader.u64();
    out.ranUeNgapId = reader.u16();
    out.causeType = reader.u8();
    out.causeValue = reader.u8();
    return reader.ok;
}

ByteBuffer encodeUeContextReleaseComplete(const UeContextReleaseComplete& complete) {
    ByteBuffer buffer;
    pushHeader(buffer, NgapProcedure::UE_CONTEXT_RELEASE_COMPLETE);
    pushU16(buffer, complete.transactionId);
    pushU64(buffer, complete.amfUeNgapId);
    pushU16(buffer, complete.ranUeNgapId);
    return buffer;
}

bool decodeUeContextReleaseComplete(const ByteBuffer& pdu, UeContextReleaseComplete& out) {
    Reader reader{pdu};
    if (!checkHeader(reader, NgapProcedure::UE_CONTEXT_RELEASE_COMPLETE)) {
        return false;
    }
    out.transactionId = reader.u16();
    out.amfUeNgapId = reader.u64();
    out.ranUeNgapId = reader.u16();
    return reader.ok;
}

}  // namespace rbs::nr