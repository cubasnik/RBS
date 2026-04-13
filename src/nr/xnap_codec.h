#pragma once

#include "../common/types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace rbs::nr {

enum class XnAPProcedure : uint8_t {
    XN_SETUP_REQUEST  = 0x01,
    XN_SETUP_RESPONSE = 0x02,
    HANDOVER_REQUEST  = 0x03,
    HANDOVER_NOTIFY   = 0x04,
};

struct XnServedCell {
    uint64_t nrCellIdentity = 0;
    uint32_t nrArfcn = 0;
    uint16_t pci = 0;
    uint16_t tac = 0;
};

struct XnSetupRequest {
    uint16_t transactionId = 0;
    uint64_t localGnbId = 0;
    std::string gnbName;
    std::vector<XnServedCell> servedCells;
};

struct XnSetupResponse {
    uint16_t transactionId = 0;
    uint64_t respondingGnbId = 0;
    std::vector<uint64_t> activatedCells;
};

struct XnHandoverRequest {
    uint16_t transactionId = 0;
    uint64_t sourceGnbId = 0;
    uint64_t targetGnbId = 0;
    uint64_t sourceCellId = 0;
    uint64_t targetCellId = 0;
    uint16_t sourceCrnti = 0;
    uint64_t ueImsi = 0;
    uint8_t causeType = 0;
    uint16_t sourceUeAmbr = 0;
    std::vector<uint8_t> pduSessionIds;
    ByteBuffer securityContext;
    ByteBuffer rrcContainer;
};

struct XnHandoverNotify {
    uint16_t transactionId = 0;
    uint64_t sourceGnbId = 0;
    uint64_t targetGnbId = 0;
    uint64_t sourceCellId = 0;
    uint64_t targetCellId = 0;
    uint16_t sourceCrnti = 0;
    uint16_t targetCrnti = 0;
    ByteBuffer rrcContainer;
};

ByteBuffer encodeXnSetupRequest(const XnSetupRequest& req);
bool decodeXnSetupRequest(const ByteBuffer& pdu, XnSetupRequest& out);

ByteBuffer encodeXnSetupResponse(const XnSetupResponse& rsp);
bool decodeXnSetupResponse(const ByteBuffer& pdu, XnSetupResponse& out);

ByteBuffer encodeXnHandoverRequest(const XnHandoverRequest& req);
bool decodeXnHandoverRequest(const ByteBuffer& pdu, XnHandoverRequest& out);

ByteBuffer encodeXnHandoverNotify(const XnHandoverNotify& notify);
bool decodeXnHandoverNotify(const ByteBuffer& pdu, XnHandoverNotify& out);

}  // namespace rbs::nr