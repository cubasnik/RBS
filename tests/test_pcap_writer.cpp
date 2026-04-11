// test_pcap_writer.cpp
// Verifies that PcapWriter produces well-formed libpcap files:
//   • correct global header (magic, version, LINKTYPE_RAW=101)
//   • correct per-record lengths for SCTP and UDP frames
//   • correct IP protocol field and key header bytes
//   • SCTP PPID at the right offset
#include "../src/common/pcap_writer.h"

#include <cassert>
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <vector>

// ── helpers ───────────────────────────────────────────────────────────────────
static uint16_t readBE16(const uint8_t* p)
{
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}

static uint32_t readBE32(const uint8_t* p)
{
    return (static_cast<uint32_t>(p[0]) << 24)
         | (static_cast<uint32_t>(p[1]) << 16)
         | (static_cast<uint32_t>(p[2]) <<  8)
         |  static_cast<uint32_t>(p[3]);
}

static uint32_t readLE32(const uint8_t* p)
{
    return  static_cast<uint32_t>(p[0])
         | (static_cast<uint32_t>(p[1]) <<  8)
         | (static_cast<uint32_t>(p[2]) << 16)
         | (static_cast<uint32_t>(p[3]) << 24);
}

static uint16_t readLE16(const uint8_t* p)
{
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Offsets inside a LINKTYPE_RAW PCAP record
//
//  IPv4 header:  bytes [0..19]  — src/dst ip, protocol, total_len, checksum
//  SCTP common:  bytes [20..31] — src/dst port, vtag, checksum
//  SCTP DATA:    bytes [32..47] — type, flags, len, TSN, sid, sseq, PPID
//  payload:      bytes [48..]
//
//  UDP:          bytes [20..27] — src/dst port, len, checksum
//  payload:      bytes [28..]
// ─────────────────────────────────────────────────────────────────────────────

static void readPacket(std::ifstream& f,
                       std::vector<uint8_t>& pkt,
                       uint32_t& inclLen)
{
    uint8_t rhdr[16];
    assert(f.read(reinterpret_cast<char*>(rhdr), 16).gcount() == 16);
    inclLen = readLE32(rhdr + 8);
    assert(inclLen == readLE32(rhdr + 12));   // incl_len == orig_len
    pkt.resize(inclLen);
    assert(f.read(reinterpret_cast<char*>(pkt.data()),
                  static_cast<std::streamsize>(inclLen))
           .gcount() == static_cast<std::streamsize>(inclLen));
}

static int test_open_close()
{
    const char* path = "test_pcap_tmp.pcap";
    rbs::PcapWriter w;
    assert(!w.isOpen());
    assert(w.open(path));
    assert(w.isOpen());
    w.close();
    assert(!w.isOpen());
    std::remove(path);
    return 0;
}

static int test_global_header()
{
    const char* path = "test_pcap_global.pcap";
    {
        rbs::PcapWriter w;
        assert(w.open(path));
    }  // destructor closes

    std::ifstream f(path, std::ios::binary);
    assert(f.is_open());

    uint8_t gh[24];
    assert(f.read(reinterpret_cast<char*>(gh), 24).gcount() == 24);

    assert(readLE32(gh +  0) == 0xa1b2c3d4u);  // magic
    assert(readLE16(gh +  4) == 2u);             // version_major
    assert(readLE16(gh +  6) == 4u);             // version_minor
    assert(readLE32(gh + 20) == 101u);           // LINKTYPE_RAW

    f.close();
    std::remove(path);
    return 0;
}

static int test_sctp_record()
{
    const char* path = "test_pcap_sctp.pcap";
    rbs::ByteBuffer payload(20, 0xAA);  // 20-byte fake S1AP

    {
        rbs::PcapWriter w;
        assert(w.open(path));
        w.writeSctp("10.0.0.1", 43210, "10.0.0.2", 36412,
                    rbs::PcapWriter::PPID_S1AP, payload);
    }

    std::ifstream f(path, std::ios::binary);
    assert(f.is_open());
    f.seekg(24);  // skip global header

    std::vector<uint8_t> pkt;
    uint32_t inclLen = 0;
    readPacket(f, pkt, inclLen);

    // IPv4 (20) + SCTP common (12) + DATA chunk hdr (16) + 20 bytes payload = 68
    // Note: 20-byte payload is already 4-byte aligned, so no padding added.
    assert(inclLen == 20 + 12 + 16 + 20);

    // IPv4: version+IHL = 0x45
    assert(pkt[0] == 0x45u);
    // IPv4: protocol = SCTP (132)
    assert(pkt[9] == 132u);
    // IPv4: total length big-endian = 68
    assert(readBE16(pkt.data() + 2) == 68u);

    // SCTP: dst port at offset 22 = 36412
    assert(readBE16(pkt.data() + 22) == 36412u);
    // SCTP: src port at offset 20 = 43210
    assert(readBE16(pkt.data() + 20) == 43210u);

    // SCTP DATA chunk: PPID at offset 44
    assert(readBE32(pkt.data() + 44) == rbs::PcapWriter::PPID_S1AP);

    f.close();
    std::remove(path);
    return 0;
}

static int test_udp_x2ap_record()
{
    const char* path = "test_pcap_udp.pcap";
    rbs::ByteBuffer payload(16, 0xBB);  // 16-byte fake X2AP frame

    {
        rbs::PcapWriter w;
        assert(w.open(path));
        w.writeUdp("10.0.0.1", 12345, "10.0.0.3", 36422, payload);
    }

    std::ifstream f(path, std::ios::binary);
    assert(f.is_open());
    f.seekg(24);

    std::vector<uint8_t> pkt;
    uint32_t inclLen = 0;
    readPacket(f, pkt, inclLen);

    // IPv4 (20) + UDP (8) + 16 = 44
    assert(inclLen == 20 + 8 + 16);
    // IPv4: protocol = UDP (17)
    assert(pkt[9] == 17u);
    // UDP: dst port at offset 22 = 36422
    assert(readBE16(pkt.data() + 22) == 36422u);

    f.close();
    std::remove(path);
    return 0;
}

static int test_udp_gtpu_record()
{
    const char* path = "test_pcap_gtp.pcap";
    rbs::ByteBuffer payload(32, 0xCC);  // 32-byte fake GTP-U frame

    {
        rbs::PcapWriter w;
        assert(w.open(path));
        w.writeUdp("10.0.0.4", 2152, "10.0.0.5", 2152, payload);
    }

    std::ifstream f(path, std::ios::binary);
    assert(f.is_open());
    f.seekg(24);

    std::vector<uint8_t> pkt;
    uint32_t inclLen = 0;
    readPacket(f, pkt, inclLen);

    // IPv4 (20) + UDP (8) + 32 = 60
    assert(inclLen == 20 + 8 + 32);
    // UDP: dst port at offset 22 = 2152 (GTP-U)
    assert(readBE16(pkt.data() + 22) == 2152u);

    f.close();
    std::remove(path);
    return 0;
}

static int test_multiple_records()
{
    const char* path = "test_pcap_multi.pcap";
    {
        rbs::PcapWriter w;
        assert(w.open(path));
        rbs::ByteBuffer p1(4, 0x11);
        rbs::ByteBuffer p2(8, 0x22);
        rbs::ByteBuffer p3(12, 0x33);
        w.writeUdp("1.2.3.4", 100, "5.6.7.8", 200, p1);
        w.writeUdp("1.2.3.4", 100, "5.6.7.8", 200, p2);
        w.writeUdp("1.2.3.4", 100, "5.6.7.8", 200, p3);
    }

    std::ifstream f(path, std::ios::binary);
    assert(f.is_open());
    f.seekg(24);

    uint32_t sizes[3] = { 20+8+4, 20+8+8, 20+8+12 };
    for (int i = 0; i < 3; ++i) {
        std::vector<uint8_t> pkt;
        uint32_t il = 0;
        readPacket(f, pkt, il);
        assert(il == sizes[i]);
    }
    // EOF after 3 records
    uint8_t probe[1];
    assert(f.read(reinterpret_cast<char*>(probe), 1).gcount() == 0);

    f.close();
    std::remove(path);
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
//  main
// ─────────────────────────────────────────────────────────────────────────────
int main()
{
    test_open_close();
    test_global_header();
    test_sctp_record();
    test_udp_x2ap_record();
    test_udp_gtpu_record();
    test_multiple_records();

    std::puts("test_pcap_writer PASSED");
    return 0;
}
