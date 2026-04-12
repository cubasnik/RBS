#include "../src/gsm/ipa.h"
#include "../src/gsm/abis_interface.h"
#include <cassert>
#include <cstdio>
#include <vector>

using namespace rbs;
using namespace rbs::gsm;

static void expectEq(const ByteBuffer& a, const ByteBuffer& b)
{
    assert(a.size() == b.size());
    for (size_t i = 0; i < a.size(); ++i) {
        assert(a[i] == b[i]);
    }
}

int main()
{
    // Golden #1: OML OPSTART (entity=0, empty payload)
    {
        ByteBuffer omlPayload{0x00};
        ByteBuffer frame = ipa::encodeFrame(0x00, static_cast<uint8_t>(OMLMsgType::OPSTART), omlPayload);
        ByteBuffer golden{0x03, 0x00, 0x00, 0x41, 0x00};
        expectEq(frame, golden);
    }

    // Golden #2: OML SET_BTS_ATTR (entity=1, payload=[0x42,0x10])
    {
        ByteBuffer omlPayload{0x01, 0x42, 0x10};
        ByteBuffer frame = ipa::encodeFrame(0x00, static_cast<uint8_t>(OMLMsgType::SET_BTS_ATTR), omlPayload);
        ByteBuffer golden{0x05, 0x00, 0x00, 0x04, 0x01, 0x42, 0x10};
        expectEq(frame, golden);
    }

    // Golden #3: RSL CHANNEL_ACTIVATION (chan/entity=3, payload=[1,0,7])
    {
        ByteBuffer rslPayload{0x03, 0x01, 0x00, 0x07};
        ByteBuffer frame = ipa::encodeFrame(0x01, static_cast<uint8_t>(RSLMsgType::CHANNEL_ACTIVATION), rslPayload);
        ByteBuffer golden{0x06, 0x00, 0x01, 0x30, 0x03, 0x01, 0x00, 0x07};
        expectEq(frame, golden);
    }

    // Parser sanity: two concatenated frames should decode as two complete IPA messages.
    {
        ByteBuffer f1 = ipa::encodeFrame(0x00, static_cast<uint8_t>(OMLMsgType::OPSTART), ByteBuffer{0x00});
        ByteBuffer f2 = ipa::encodeFrame(0x01, static_cast<uint8_t>(RSLMsgType::PAGING_CMD), ByteBuffer{0x00, 0x21, 0x43});
        ByteBuffer merged;
        merged.insert(merged.end(), f1.begin(), f1.end());
        merged.insert(merged.end(), f2.begin(), f2.end());

        ipa::FrameParser parser;
        const size_t n = parser.parse(merged);
        assert(n == 2);
        assert(parser.frameAt(0).msgFilter == 0x00);
        assert(parser.frameAt(0).msgType == static_cast<uint8_t>(OMLMsgType::OPSTART));
        assert(parser.frameAt(1).msgFilter == 0x01);
        assert(parser.frameAt(1).msgType == static_cast<uint8_t>(RSLMsgType::PAGING_CMD));
    }

    std::puts("test_abis_ipa_golden PASSED");
    return 0;
}
