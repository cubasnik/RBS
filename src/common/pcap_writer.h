#pragma once
#include "types.h"
#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>

namespace rbs {

/// PCAP file writer (libpcap format, magic 0xa1b2c3d4, microsecond timestamps).
///
/// Link type LINKTYPE_RAW (101) = raw IPv4 datagrams, no Ethernet header.
/// Wireshark auto-dissects IP → SCTP/UDP → S1AP/X2AP/GTP-U by protocol
/// field and port numbers.
///
/// Thread-safe: all write methods are mutex-guarded.
class PcapWriter {
public:
    /// S1AP SCTP PPID per TS 29.202 §7
    static constexpr uint32_t PPID_S1AP = 18u;
    /// X2AP SCTP PPID per TS 36.422
    static constexpr uint32_t PPID_X2AP = 27u;

    PcapWriter() = default;
    ~PcapWriter() { close(); }

    PcapWriter(const PcapWriter&)            = delete;
    PcapWriter& operator=(const PcapWriter&) = delete;

    /// Create/overwrite @path and write the PCAP global header.
    /// Returns false on I/O error.
    bool open(const std::string& path);

    /// Flush and close the file.
    void close();

    bool isOpen() const;

    /// Write a pre-assembled raw IPv4 datagram (LINKTYPE_RAW frame)
    /// with a current-time timestamp.
    void writePacket(const ByteBuffer& ipDatagram);

    /// Build IPv4 + SCTP DATA chunk + @payload and write.
    /// @ppid  SCTP Payload Protocol Identifier (PPID_S1AP=18, PPID_X2AP=27)
    void writeSctp(const std::string& srcIp, uint16_t srcPort,
                   const std::string& dstIp,  uint16_t dstPort,
                   uint32_t ppid, const ByteBuffer& payload);

    /// Build IPv4 + UDP + @payload and write.
    /// Use for X2AP-over-UDP (port 36422) and GTP-U (port 2152).
    void writeUdp(const std::string& srcIp, uint16_t srcPort,
                  const std::string& dstIp,  uint16_t dstPort,
                  const ByteBuffer& payload);

private:
    mutable std::mutex  mtx_;
    std::ofstream       file_;
    bool                open_ = false;

    // Caller must hold mtx_
    void writeRecord(const ByteBuffer& pkt);

    static void       appendBE16(ByteBuffer& b, uint16_t v);
    static void       appendBE32(ByteBuffer& b, uint32_t v);
    static uint32_t   parseIPv4(const std::string& ip);   // returns host order
    static uint16_t   ipv4Checksum(const ByteBuffer& hdr);
    static ByteBuffer buildIPv4Hdr(uint32_t srcIp, uint32_t dstIp,
                                   uint8_t proto, uint16_t payloadLen);
};

} // namespace rbs
