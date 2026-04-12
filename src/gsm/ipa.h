#pragma once

#include "../common/types.h"
#include "../common/logger.h"
#include <cstdint>
#include <vector>
#include <cstring>

namespace rbs::gsm::ipa {

/// IPA (IP Protocol for Abis) — 3GPP TS 12.21 §4.2
/// Simple framing protocol for OML/RSL over IP.
///
/// Frame format (little-endian length):
/// +------+-----+-------+-------+--------+
/// | Len  |Len  | MsgFilter | TCP Msg | Payload |
/// | (LE) |(H)  | (1 byte)  | Type(1) |  data   |
/// +------+-----+-------+-------+--------+
///  (2)   (1)   (1)    (1)     (variable)
///
/// Total frame size = 2 + len (where len includes MsgFilter + MsgType + payload)

struct Frame {
    uint16_t len = 0;          // Data length (MsgFilter + MsgType + payload), little-endian
    uint8_t msgFilter = 0x00;  // Message filter (usually 0x00 for OML)
    uint8_t msgType = 0x00;    // Message type (OML/RSL discriminator)
    ByteBuffer payload;        // Remaining data
};

/// Encode an OML message into IPA frame format.
/// 
/// @param msgFilter  Filter byte (0x00 for OML)
/// @param msgType    Type discriminator (OMLMsgType cast as uint8_t)
/// @param payload    Message data
/// @return           Encoded frame ready to send
inline ByteBuffer encodeFrame(uint8_t msgFilter, uint8_t msgType, const ByteBuffer& payload) {
    ByteBuffer frame;
    
    // Calculate total data length: filter (1) + type (1) + payload
    const size_t rawLen = 2 + payload.size();
    if (rawLen > 0xFFFF) {
        RBS_LOG_ERROR("IPA", "Payload too large for IPA frame: {}", rawLen);
        return {};
    }
    const uint16_t dataLen = static_cast<uint16_t>(rawLen);
    
    // IPA uses little-endian 16-bit length
    frame.push_back(dataLen & 0xFF);         // Length low byte
    frame.push_back((dataLen >> 8) & 0xFF);  // Length high byte
    frame.push_back(msgFilter);
    frame.push_back(msgType);
    frame.insert(frame.end(), payload.begin(), payload.end());
    
    return frame;
}

/// Decode an entire IPA frame from a buffer.
/// Handles partial frames gracefully.
///
/// @param buffer      Input buffer to parse
/// @param consumed    Output: number of bytes consumed from buffer
/// @param frame       Output: decoded frame (valid only if return true)
/// @return            true if complete frame decoded, false if partial
inline bool decodeFrame(const ByteBuffer& buffer, size_t& consumed, Frame& frame) {
    consumed = 0;
    
    // Need at least header (4 bytes: len_lo, len_hi, filter, type)
    if (buffer.size() < 4) {
        return false;
    }
    
    // Extract little-endian length
    uint16_t dataLen = buffer[0] | (buffer[1] << 8);
    
    // Sanity check: max IPA frame is typically ~65KB
    if (dataLen > 65000) {
        RBS_LOG_ERROR("IPA", "Frame length too large: {}", dataLen);
        return false;
    }
    
    // Check if we have entire frame: length field (2) + data (dataLen)
    size_t frameSize = 2 + dataLen;
    if (buffer.size() < frameSize) {
        return false;  // Partial frame, wait for more
    }
    
    // Decode
    frame.len = dataLen;
    frame.msgFilter = buffer[2];
    frame.msgType = buffer[3];
    frame.payload.assign(buffer.begin() + 4, buffer.begin() + frameSize);
    consumed = frameSize;
    
    return true;
}

/// Full state machine for streaming TCP data → IPA frames.
/// Handles fragmentation and reassembly.
class FrameParser {
private:
    ByteBuffer rxBuf_;  // Receive buffer for partial frames

public:
    /// Parse incoming TCP data, extract complete frames.
    /// Returns count of frames parsed; call frameAt(i) to get them.
    ///
    /// @param tcpData   Raw TCP data received
    /// @return          Number of complete frames extracted
    size_t parse(const ByteBuffer& tcpData) {
        rxBuf_.insert(rxBuf_.end(), tcpData.begin(), tcpData.end());
        
        frames_.clear();
        
        size_t offset = 0;
        while (offset < rxBuf_.size()) {
            size_t consumed = 0;
            Frame frame;
            
            ByteBuffer remaining(rxBuf_.begin() + offset, rxBuf_.end());
            
            if (!decodeFrame(remaining, consumed, frame)) {
                // Partial frame; save remainder for next call
                rxBuf_.erase(rxBuf_.begin(), rxBuf_.begin() + offset);
                return frames_.size();
            }
            
            frames_.push_back(frame);
            offset += consumed;
        }
        
        // Clear the buffer once all frames are processed
        rxBuf_.clear();
        return frames_.size();
    }
    
    /// Get i-th parsed frame
    const Frame& frameAt(size_t i) const {
        return frames_[i];
    }
    
    /// Clear the internal buffer (for error recovery)
    void clear() {
        rxBuf_.clear();
        frames_.clear();
    }

private:
    std::vector<Frame> frames_;
};

} // namespace rbs::gsm::ipa
