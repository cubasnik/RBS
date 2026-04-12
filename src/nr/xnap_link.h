#pragma once

#include "xnap_codec.h"

#include <cstdint>
#include <mutex>
#include <queue>
#include <string>
#include <unordered_map>

namespace rbs::nr {

struct XnAPMessage {
    XnAPProcedure procedure = XnAPProcedure::XN_SETUP_REQUEST;
    uint64_t sourceGnbId = 0;
    uint64_t targetGnbId = 0;
    ByteBuffer payload;
};

class XnAPLink {
public:
    explicit XnAPLink(uint64_t localGnbId, std::string gnbName = {});
    ~XnAPLink();

    bool connect(uint64_t targetGnbId);
    bool isConnected(uint64_t targetGnbId) const;

    bool xnSetup(uint64_t targetGnbId, const XnSetupRequest& req);
    bool xnSetupResponse(uint64_t targetGnbId, const XnSetupResponse& rsp);
    bool handoverRequest(const XnHandoverRequest& req);
    bool handoverNotify(const XnHandoverNotify& notify);

    bool recvXnApMessage(XnAPMessage& msg);

    uint64_t localGnbId() const { return localGnbId_; }
    const std::string& gnbName() const { return gnbName_; }

private:
    bool sendMessage(uint64_t targetGnbId, XnAPProcedure procedure, const ByteBuffer& payload);
    void enqueue(XnAPMessage&& msg);

    uint64_t localGnbId_;
    std::string gnbName_;
    std::unordered_map<uint64_t, bool> peers_;
    mutable std::mutex rxMutex_;
    std::queue<XnAPMessage> rxQueue_;

    static std::mutex registryMutex_;
    static std::unordered_map<uint64_t, XnAPLink*> registry_;
};

}  // namespace rbs::nr