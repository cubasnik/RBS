#pragma once

#include "../common/types.h"

#include <cstdint>
#include <string>

namespace rbs::lte::volte {

enum class SipMethod : uint8_t {
    UNKNOWN = 0,
    REGISTER,
    INVITE,
    BYE,
};

struct SipMessage {
    SipMethod method = SipMethod::UNKNOWN;
    std::string callId;
    std::string from;
    std::string to;
    std::string body;
};

std::string buildRegister(const std::string& fromUri, const std::string& callId);
std::string buildInvite(const std::string& fromUri, const std::string& toUri,
                        const std::string& callId, const std::string& sdp);
std::string buildBye(const std::string& fromUri, const std::string& toUri,
                     const std::string& callId);
SipMessage parseMessage(const std::string& text);

struct RtpHeader {
    uint8_t  payloadType = 96;
    bool     marker = false;
    uint16_t sequence = 0;
    uint32_t timestamp = 0;
    uint32_t ssrc = 0;
};

struct RtcpHeader {
    uint8_t  packetType = 200;
    uint8_t  reportCount = 0;
    uint16_t length = 0;
    uint32_t ssrc = 0;
};

ByteBuffer encodeRtp(const RtpHeader& h, const ByteBuffer& payload);
ByteBuffer encodeRtcp(const RtcpHeader& h, const ByteBuffer& payload = {});

}  // namespace rbs::lte::volte
