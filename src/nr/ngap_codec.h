#pragma once

#include "../common/types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace rbs::nr {

enum class NgapProcedure : uint8_t {
    NG_SETUP_REQUEST = 0x01,
    NG_SETUP_RESPONSE = 0x02,
    PDU_SESSION_SETUP_REQUEST = 0x03,
    PDU_SESSION_SETUP_RESPONSE = 0x04,
    UE_CONTEXT_RELEASE_COMMAND = 0x05,
    UE_CONTEXT_RELEASE_COMPLETE = 0x06,
    PAGING = 0x07,
};

struct NgSetupRequest {
    uint16_t transactionId = 0;
    uint64_t ranNodeId = 0;
    std::string gnbName;
    uint16_t tac = 0;
    uint16_t mcc = 0;
    uint16_t mnc = 0;
};

struct NgSetupResponse {
    uint16_t transactionId = 0;
    uint64_t amfId = 0;
    std::string amfName;
    uint16_t relativeCapacity = 255;
};

struct PduSessionSetupRequest {
    uint16_t transactionId = 0;
    uint64_t amfUeNgapId = 0;
    uint16_t ranUeNgapId = 0;
    uint8_t pduSessionId = 0;
    uint8_t sst = 1;
    uint32_t sd = 0;
    ByteBuffer nasPdu;
};

struct PduSessionSetupResponse {
    uint16_t transactionId = 0;
    uint64_t amfUeNgapId = 0;
    uint16_t ranUeNgapId = 0;
    uint8_t pduSessionId = 0;
    uint32_t gtpTeid = 0;
    ByteBuffer transfer;
};

struct UeContextReleaseCommand {
    uint16_t transactionId = 0;
    uint64_t amfUeNgapId = 0;
    uint16_t ranUeNgapId = 0;
    uint8_t causeType = 0;
    uint8_t causeValue = 0;
    uint8_t releaseAction = 0;
    ByteBuffer contextInfo;
};

struct UeContextReleaseComplete {
    uint16_t transactionId = 0;
    uint64_t amfUeNgapId = 0;
    uint16_t ranUeNgapId = 0;
    ByteBuffer releaseReport;
};

struct PagingMessage {
    uint16_t transactionId = 0;
    uint64_t uePagingIdentity = 0;
    uint32_t fivegTmsi = 0;
    uint16_t tac = 0;
    uint16_t mcc = 0;
    uint16_t mnc = 0;
    uint8_t pagingPriority = 0;
    uint16_t drxCycle = 128;
};

ByteBuffer encodeNgSetupRequest(const NgSetupRequest& req);
bool decodeNgSetupRequest(const ByteBuffer& pdu, NgSetupRequest& out);

ByteBuffer encodeNgSetupResponse(const NgSetupResponse& rsp);
bool decodeNgSetupResponse(const ByteBuffer& pdu, NgSetupResponse& out);

ByteBuffer encodePduSessionSetupRequest(const PduSessionSetupRequest& req);
bool decodePduSessionSetupRequest(const ByteBuffer& pdu, PduSessionSetupRequest& out);

ByteBuffer encodePduSessionSetupResponse(const PduSessionSetupResponse& rsp);
bool decodePduSessionSetupResponse(const ByteBuffer& pdu, PduSessionSetupResponse& out);

ByteBuffer encodeUeContextReleaseCommand(const UeContextReleaseCommand& cmd);
bool decodeUeContextReleaseCommand(const ByteBuffer& pdu, UeContextReleaseCommand& out);

ByteBuffer encodeUeContextReleaseComplete(const UeContextReleaseComplete& complete);
bool decodeUeContextReleaseComplete(const ByteBuffer& pdu, UeContextReleaseComplete& out);

ByteBuffer encodePagingMessage(const PagingMessage& paging);
bool decodePagingMessage(const ByteBuffer& pdu, PagingMessage& out);

}  // namespace rbs::nr