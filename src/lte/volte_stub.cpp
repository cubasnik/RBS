#include "volte_stub.h"

#include <algorithm>
#include <sstream>

namespace rbs::lte::volte {

static std::string getHeaderValue(const std::string& text, const std::string& key) {
    const std::string needle = key + ":";
    auto pos = text.find(needle);
    if (pos == std::string::npos) return {};
    pos += needle.size();
    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t')) ++pos;
    auto end = text.find("\r\n", pos);
    if (end == std::string::npos) end = text.find('\n', pos);
    if (end == std::string::npos) end = text.size();
    return text.substr(pos, end - pos);
}

static std::string buildCommon(const std::string& methodLine,
                               const std::string& fromUri,
                               const std::string& toUri,
                               const std::string& callId,
                               const std::string& cseqMethod,
                               const std::string& body) {
    std::ostringstream os;
    os << methodLine << "\r\n"
       << "Via: SIP/2.0/UDP rbs.local;branch=z9hG4bK-rbs\r\n"
       << "From: <" << fromUri << ">\r\n"
       << "To: <" << toUri << ">\r\n"
       << "Call-ID: " << callId << "\r\n"
    << "CSeq: 1 " << cseqMethod << "\r\n"
       << "Contact: <" << fromUri << ">\r\n"
       << "Content-Length: " << body.size() << "\r\n"
       << "\r\n"
       << body;
    return os.str();
}

std::string buildRegister(const std::string& fromUri, const std::string& callId) {
    return buildCommon("REGISTER sip:ims.local SIP/2.0", fromUri, "sip:ims.local", callId,
                       "REGISTER", "");
}

std::string buildInvite(const std::string& fromUri, const std::string& toUri,
                        const std::string& callId, const std::string& sdp) {
    return buildCommon("INVITE " + toUri + " SIP/2.0", fromUri, toUri, callId,
                       "INVITE", sdp);
}

std::string buildBye(const std::string& fromUri, const std::string& toUri,
                     const std::string& callId) {
    return buildCommon("BYE " + toUri + " SIP/2.0", fromUri, toUri, callId,
                       "BYE", "");
}

SipMessage parseMessage(const std::string& text) {
    SipMessage msg{};
    const auto lineEnd = text.find("\r\n");
    const std::string first = (lineEnd == std::string::npos) ? text : text.substr(0, lineEnd);
    if (first.rfind("REGISTER ", 0) == 0) msg.method = SipMethod::REGISTER;
    else if (first.rfind("INVITE ", 0) == 0) msg.method = SipMethod::INVITE;
    else if (first.rfind("BYE ", 0) == 0) msg.method = SipMethod::BYE;

    msg.callId = getHeaderValue(text, "Call-ID");
    msg.from = getHeaderValue(text, "From");
    msg.to = getHeaderValue(text, "To");
    const auto bodyPos = text.find("\r\n\r\n");
    if (bodyPos != std::string::npos) {
        msg.body = text.substr(bodyPos + 4);
    }
    return msg;
}

ByteBuffer encodeRtp(const RtpHeader& h, const ByteBuffer& payload) {
    ByteBuffer out;
    out.reserve(12 + payload.size());
    out.push_back(static_cast<uint8_t>(0x80));  // V=2
    out.push_back(static_cast<uint8_t>((h.marker ? 0x80 : 0x00) | (h.payloadType & 0x7F)));
    out.push_back(static_cast<uint8_t>((h.sequence >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(h.sequence & 0xFF));
    out.push_back(static_cast<uint8_t>((h.timestamp >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((h.timestamp >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((h.timestamp >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(h.timestamp & 0xFF));
    out.push_back(static_cast<uint8_t>((h.ssrc >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((h.ssrc >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((h.ssrc >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(h.ssrc & 0xFF));
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

ByteBuffer encodeRtcp(const RtcpHeader& h, const ByteBuffer& payload) {
    ByteBuffer out;
    out.reserve(8 + payload.size());
    out.push_back(static_cast<uint8_t>(0x80 | (h.reportCount & 0x1F)));  // V=2
    out.push_back(h.packetType);
    const uint16_t words = h.length ? h.length
                                    : static_cast<uint16_t>((payload.size() + 8) / 4 - 1);
    out.push_back(static_cast<uint8_t>((words >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(words & 0xFF));
    out.push_back(static_cast<uint8_t>((h.ssrc >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((h.ssrc >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((h.ssrc >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(h.ssrc & 0xFF));
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

}  // namespace rbs::lte::volte
