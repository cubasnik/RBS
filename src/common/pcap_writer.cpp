#include "pcap_writer.h"
#include "logger.h"

#include <chrono>

namespace rbs {

// ─────────────────────────────────────────────────────────────────────────────
//  Internal PCAP I/O helpers (write directly to ofstream, not into a ByteBuffer)
// ─────────────────────────────────────────────────────────────────────────────
static void writeU16LE(std::ofstream& f, uint16_t v)
{
    const uint8_t b[2] = { static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8) };
    f.write(reinterpret_cast<const char*>(b), 2);
}

static void writeU32LE(std::ofstream& f, uint32_t v)
{
    const uint8_t b[4] = {
        static_cast<uint8_t>(v),
        static_cast<uint8_t>(v >>  8),
        static_cast<uint8_t>(v >> 16),
        static_cast<uint8_t>(v >> 24)
    };
    f.write(reinterpret_cast<const char*>(b), 4);
}

// ─────────────────────────────────────────────────────────────────────────────
//  PcapWriter public API
// ─────────────────────────────────────────────────────────────────────────────
bool PcapWriter::open(const std::string& path)
{
    std::lock_guard<std::mutex> lk(mtx_);
    if (open_) {
        file_.flush();
        file_.close();
        open_ = false;
    }
    file_.open(path, std::ios::binary | std::ios::trunc);
    if (!file_.is_open()) {
        RBS_LOG_ERROR("PCAP", "cannot open {}", path);
        return false;
    }

    // Global PCAP header — 24 bytes, all little-endian
    // Reference: https://wiki.wireshark.org/Development/LibpcapFileFormat
    writeU32LE(file_, 0xa1b2c3d4u);  // magic   (µs timestamps)
    writeU16LE(file_, 2u);            // version_major
    writeU16LE(file_, 4u);            // version_minor
    writeU32LE(file_, 0u);            // thiszone  (GMT)
    writeU32LE(file_, 0u);            // sigfigs
    writeU32LE(file_, 65535u);        // snaplen
    writeU32LE(file_, 101u);          // network  (LINKTYPE_RAW = raw IPv4)

    open_ = true;
    RBS_LOG_INFO("PCAP", "trace opened: {}", path);
    return true;
}

void PcapWriter::close()
{
    std::lock_guard<std::mutex> lk(mtx_);
    if (!open_) return;
    file_.flush();
    file_.close();
    open_ = false;
}

bool PcapWriter::isOpen() const
{
    std::lock_guard<std::mutex> lk(mtx_);
    return open_;
}

void PcapWriter::writePacket(const ByteBuffer& ipDatagram)
{
    std::lock_guard<std::mutex> lk(mtx_);
    if (!open_) return;
    writeRecord(ipDatagram);
}

// ─────────────────────────────────────────────────────────────────────────────
//  PCAP record (caller must hold mtx_)
// ─────────────────────────────────────────────────────────────────────────────
void PcapWriter::writeRecord(const ByteBuffer& pkt)
{
    using namespace std::chrono;
    const auto now  = system_clock::now().time_since_epoch();
    const auto sec  = static_cast<uint32_t>(duration_cast<seconds>(now).count());
    const auto usec = static_cast<uint32_t>(
                          duration_cast<microseconds>(now).count() % 1'000'000u);
    const auto len  = static_cast<uint32_t>(pkt.size());

    writeU32LE(file_, sec);
    writeU32LE(file_, usec);
    writeU32LE(file_, len);   // incl_len
    writeU32LE(file_, len);   // orig_len
    file_.write(reinterpret_cast<const char*>(pkt.data()),
                static_cast<std::streamsize>(len));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Static helpers
// ─────────────────────────────────────────────────────────────────────────────
void PcapWriter::appendBE16(ByteBuffer& b, uint16_t v)
{
    b.push_back(static_cast<uint8_t>(v >> 8));
    b.push_back(static_cast<uint8_t>(v));
}

void PcapWriter::appendBE32(ByteBuffer& b, uint32_t v)
{
    b.push_back(static_cast<uint8_t>(v >> 24));
    b.push_back(static_cast<uint8_t>(v >> 16));
    b.push_back(static_cast<uint8_t>(v >>  8));
    b.push_back(static_cast<uint8_t>(v));
}

/// Parse "a.b.c.d" → packed host-order uint32_t (high byte = a).
uint32_t PcapWriter::parseIPv4(const std::string& ip)
{
    uint32_t result = 0;
    int      octet  = 0;
    for (unsigned char c : ip) {
        if (c >= '0' && c <= '9') {
            octet = octet * 10 + (c - '0');
        } else if (c == '.') {
            result = (result << 8) | static_cast<uint8_t>(octet);
            octet  = 0;
        }
    }
    return (result << 8) | static_cast<uint8_t>(octet);
}

/// One's-complement checksum of a 20-byte IPv4 header (checksum field = 0).
uint16_t PcapWriter::ipv4Checksum(const ByteBuffer& hdr)
{
    uint32_t sum = 0;
    for (size_t i = 0; i + 1 < hdr.size(); i += 2)
        sum += (static_cast<uint32_t>(hdr[i]) << 8) | hdr[i + 1];
    if (hdr.size() & 1u)
        sum += static_cast<uint32_t>(hdr.back()) << 8;
    while (sum >> 16)
        sum = (sum & 0xFFFFu) + (sum >> 16);
    return static_cast<uint16_t>(~sum);
}

/// Build a 20-byte IPv4 header with DF set and computed checksum.
ByteBuffer PcapWriter::buildIPv4Hdr(uint32_t srcIp, uint32_t dstIp,
                                     uint8_t proto, uint16_t payloadLen)
{
    ByteBuffer h;
    h.reserve(20);
    h.push_back(0x45u);                                               // ver=4, IHL=5
    h.push_back(0x00u);                                               // DSCP/ECN
    appendBE16(h, static_cast<uint16_t>(20u + payloadLen));           // total length
    appendBE16(h, 0x0000u);                                           // id
    appendBE16(h, 0x4000u);                                           // DF, frag=0
    h.push_back(64u);                                                 // TTL
    h.push_back(proto);                                               // protocol
    appendBE16(h, 0x0000u);                                           // checksum (filled below)
    appendBE32(h, srcIp);                                             // source
    appendBE32(h, dstIp);                                             // destination

    const uint16_t csum = ipv4Checksum(h);
    h[10] = static_cast<uint8_t>(csum >> 8);
    h[11] = static_cast<uint8_t>(csum);
    return h;
}

// ─────────────────────────────────────────────────────────────────────────────
//  SCTP write  (S1AP PPID=18, X2AP-over-SCTP PPID=27)
// ─────────────────────────────────────────────────────────────────────────────
void PcapWriter::writeSctp(const std::string& srcIp, uint16_t srcPort,
                            const std::string& dstIp,  uint16_t dstPort,
                            uint32_t ppid, const ByteBuffer& payload)
{
    // SCTP common header (12 B) + DATA chunk header (16 B) + payload + padding
    ByteBuffer sctp;
    sctp.reserve(12 + 16 + payload.size() + 3u);

    // Common header
    appendBE16(sctp, srcPort);
    appendBE16(sctp, dstPort);
    appendBE32(sctp, 0u);   // verification tag (0 in simulator)
    appendBE32(sctp, 0u);   // checksum = 0 (Wireshark: Edit→Preferences→SCTP→verify=off)

    // DATA chunk  (type=0x00, flags=0x03 = B|E = single-fragment message)
    const uint16_t chunkLen = static_cast<uint16_t>(16u + payload.size());
    sctp.push_back(0x00u);            // chunk type = DATA
    sctp.push_back(0x03u);            // B+E flags
    appendBE16(sctp, chunkLen);
    appendBE32(sctp, 0u);             // TSN
    appendBE16(sctp, 0u);             // stream id
    appendBE16(sctp, 0u);             // stream sequence
    appendBE32(sctp, ppid);           // PPID

    sctp.insert(sctp.end(), payload.begin(), payload.end());

    // Pad chunk to 4-byte boundary
    while (sctp.size() % 4u != 0) sctp.push_back(0u);

    const auto ipHdr = buildIPv4Hdr(parseIPv4(srcIp), parseIPv4(dstIp),
                                    132u, static_cast<uint16_t>(sctp.size()));
    ByteBuffer pkt = ipHdr;
    pkt.insert(pkt.end(), sctp.begin(), sctp.end());

    std::lock_guard<std::mutex> lk(mtx_);
    if (!open_) return;
    writeRecord(pkt);
}

// ─────────────────────────────────────────────────────────────────────────────
//  UDP write  (X2AP port 36422 / GTP-U port 2152)
// ─────────────────────────────────────────────────────────────────────────────
void PcapWriter::writeUdp(const std::string& srcIp, uint16_t srcPort,
                           const std::string& dstIp,  uint16_t dstPort,
                           const ByteBuffer& payload)
{
    ByteBuffer udp;
    udp.reserve(8u + payload.size());
    appendBE16(udp, srcPort);
    appendBE16(udp, dstPort);
    appendBE16(udp, static_cast<uint16_t>(8u + payload.size()));  // length
    appendBE16(udp, 0u);                                           // checksum = 0

    udp.insert(udp.end(), payload.begin(), payload.end());

    const auto ipHdr = buildIPv4Hdr(parseIPv4(srcIp), parseIPv4(dstIp),
                                    17u, static_cast<uint16_t>(udp.size()));
    ByteBuffer pkt = ipHdr;
    pkt.insert(pkt.end(), udp.begin(), udp.end());

    std::lock_guard<std::mutex> lk(mtx_);
    if (!open_) return;
    writeRecord(pkt);
}

} // namespace rbs
