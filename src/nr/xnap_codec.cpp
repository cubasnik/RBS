#include "xnap_codec.h"

namespace rbs::nr {

namespace {

constexpr uint8_t XNAP_MAGIC0 = 0x38;
constexpr uint8_t XNAP_MAGIC1 = 0x42;

void pushU8(ByteBuffer& buffer, uint8_t value) {
    buffer.push_back(value);
}

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
    const uint16_t length = static_cast<uint16_t>(value.size() > 0xFFFF ? 0xFFFF : value.size());
    pushU16(buffer, length);
    buffer.insert(buffer.end(), value.begin(), value.begin() + length);
}

void pushBytes(ByteBuffer& buffer, const ByteBuffer& value) {
    const uint16_t length = static_cast<uint16_t>(value.size() > 0xFFFF ? 0xFFFF : value.size());
    pushU16(buffer, length);
    buffer.insert(buffer.end(), value.begin(), value.begin() + length);
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
        const uint16_t length = u16();
        if (!ok || position + length > buffer.size()) {
            ok = false;
            return {};
        }
        std::string value(reinterpret_cast<const char*>(&buffer[position]), length);
        position += length;
        return value;
    }

    ByteBuffer bytes() {
        const uint16_t length = u16();
        if (!ok || position + length > buffer.size()) {
            ok = false;
            return {};
        }
        ByteBuffer value(buffer.begin() + static_cast<std::ptrdiff_t>(position),
                         buffer.begin() + static_cast<std::ptrdiff_t>(position + length));
        position += length;
        return value;
    }
};

void pushHeader(ByteBuffer& buffer, XnAPProcedure procedure) {
    pushU8(buffer, XNAP_MAGIC0);
    pushU8(buffer, XNAP_MAGIC1);
    pushU8(buffer, static_cast<uint8_t>(procedure));
}

bool checkHeader(Reader& reader, XnAPProcedure procedure) {
    return reader.u8() == XNAP_MAGIC0
        && reader.u8() == XNAP_MAGIC1
        && reader.u8() == static_cast<uint8_t>(procedure)
        && reader.ok;
}

}  // namespace

ByteBuffer encodeXnSetupRequest(const XnSetupRequest& req) {
    ByteBuffer buffer;
    pushHeader(buffer, XnAPProcedure::XN_SETUP_REQUEST);
    pushU16(buffer, req.transactionId);
    pushU64(buffer, req.localGnbId);
    pushStr(buffer, req.gnbName);
    const uint8_t count = static_cast<uint8_t>(req.servedCells.size() > 255 ? 255 : req.servedCells.size());
    pushU8(buffer, count);
    for (uint8_t index = 0; index < count; ++index) {
        const auto& cell = req.servedCells[index];
        pushU64(buffer, cell.nrCellIdentity);
        pushU32(buffer, cell.nrArfcn);
        pushU16(buffer, cell.pci);
        pushU16(buffer, cell.tac);
    }
    return buffer;
}

bool decodeXnSetupRequest(const ByteBuffer& pdu, XnSetupRequest& out) {
    Reader reader{pdu};
    if (!checkHeader(reader, XnAPProcedure::XN_SETUP_REQUEST)) {
        return false;
    }
    out.transactionId = reader.u16();
    out.localGnbId = reader.u64();
    out.gnbName = reader.str();
    const uint8_t count = reader.u8();
    out.servedCells.clear();
    for (uint8_t index = 0; index < count && reader.ok; ++index) {
        XnServedCell cell{};
        cell.nrCellIdentity = reader.u64();
        cell.nrArfcn = reader.u32();
        cell.pci = reader.u16();
        cell.tac = reader.u16();
        out.servedCells.push_back(cell);
    }
    return reader.ok;
}

ByteBuffer encodeXnSetupResponse(const XnSetupResponse& rsp) {
    ByteBuffer buffer;
    pushHeader(buffer, XnAPProcedure::XN_SETUP_RESPONSE);
    pushU16(buffer, rsp.transactionId);
    pushU64(buffer, rsp.respondingGnbId);
    const uint8_t count = static_cast<uint8_t>(rsp.activatedCells.size() > 255 ? 255 : rsp.activatedCells.size());
    pushU8(buffer, count);
    for (uint8_t index = 0; index < count; ++index) {
        pushU64(buffer, rsp.activatedCells[index]);
    }
    return buffer;
}

bool decodeXnSetupResponse(const ByteBuffer& pdu, XnSetupResponse& out) {
    Reader reader{pdu};
    if (!checkHeader(reader, XnAPProcedure::XN_SETUP_RESPONSE)) {
        return false;
    }
    out.transactionId = reader.u16();
    out.respondingGnbId = reader.u64();
    const uint8_t count = reader.u8();
    out.activatedCells.clear();
    for (uint8_t index = 0; index < count && reader.ok; ++index) {
        out.activatedCells.push_back(reader.u64());
    }
    return reader.ok;
}

ByteBuffer encodeXnHandoverRequest(const XnHandoverRequest& req) {
    ByteBuffer buffer;
    pushHeader(buffer, XnAPProcedure::HANDOVER_REQUEST);
    pushU16(buffer, req.transactionId);
    pushU64(buffer, req.sourceGnbId);
    pushU64(buffer, req.targetGnbId);
    pushU64(buffer, req.sourceCellId);
    pushU64(buffer, req.targetCellId);
    pushU16(buffer, req.sourceCrnti);
    pushU64(buffer, req.ueImsi);
    pushU8(buffer, req.causeType);
    pushBytes(buffer, req.rrcContainer);
    return buffer;
}

bool decodeXnHandoverRequest(const ByteBuffer& pdu, XnHandoverRequest& out) {
    Reader reader{pdu};
    if (!checkHeader(reader, XnAPProcedure::HANDOVER_REQUEST)) {
        return false;
    }
    out.transactionId = reader.u16();
    out.sourceGnbId = reader.u64();
    out.targetGnbId = reader.u64();
    out.sourceCellId = reader.u64();
    out.targetCellId = reader.u64();
    out.sourceCrnti = reader.u16();
    out.ueImsi = reader.u64();
    out.causeType = reader.u8();
    out.rrcContainer = reader.bytes();
    return reader.ok;
}

ByteBuffer encodeXnHandoverNotify(const XnHandoverNotify& notify) {
    ByteBuffer buffer;
    pushHeader(buffer, XnAPProcedure::HANDOVER_NOTIFY);
    pushU16(buffer, notify.transactionId);
    pushU64(buffer, notify.sourceGnbId);
    pushU64(buffer, notify.targetGnbId);
    pushU64(buffer, notify.sourceCellId);
    pushU64(buffer, notify.targetCellId);
    pushU16(buffer, notify.sourceCrnti);
    pushU16(buffer, notify.targetCrnti);
    pushBytes(buffer, notify.rrcContainer);
    return buffer;
}

bool decodeXnHandoverNotify(const ByteBuffer& pdu, XnHandoverNotify& out) {
    Reader reader{pdu};
    if (!checkHeader(reader, XnAPProcedure::HANDOVER_NOTIFY)) {
        return false;
    }
    out.transactionId = reader.u16();
    out.sourceGnbId = reader.u64();
    out.targetGnbId = reader.u64();
    out.sourceCellId = reader.u64();
    out.targetCellId = reader.u64();
    out.sourceCrnti = reader.u16();
    out.targetCrnti = reader.u16();
    out.rrcContainer = reader.bytes();
    return reader.ok;
}

}  // namespace rbs::nr