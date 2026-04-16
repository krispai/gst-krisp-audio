///
/// Copyright Krisp, Inc
///
/// Unit tests for core C++ logic in krisp_session.hpp:
///   - toKrispRate() enum converter
///   - toKrispFrameDuration() enum converter
///   - KrispSession<T,S> carry-buffer FIFO
///
/// No Krisp SDK license key or model files are required.
///
#include "krisp_session.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>

// ---------------------------------------------------------------------------
// Assertion helpers
// ---------------------------------------------------------------------------

// clang-format off
#define ASSERT(expr)                                                                   \
    do                                                                                 \
    {                                                                                  \
        if (!(expr))                                                                   \
        {                                                                              \
            std::fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr);   \
            return 1;                                                                  \
        }                                                                              \
    } while (0)

#define ASSERT_THROWS(expr)                                                            \
    do                                                                                 \
    {                                                                                  \
        bool threw = false;                                                            \
        try { (expr); }                                                                \
        catch (const std::exception&) { threw = true; }                               \
        if (!threw)                                                                    \
        {                                                                              \
            std::fprintf(stderr, "  FAIL %s:%d: expected throw: %s\n",               \
                __FILE__, __LINE__, #expr);                                            \
            return 1;                                                                  \
        }                                                                              \
    } while (0)
// clang-format on

// ---------------------------------------------------------------------------
// Pass-through mock session
//
// Copies input to output unchanged. Lets KrispSession's FIFO logic be tested
// in isolation without any real Krisp SDK session or model.
// ---------------------------------------------------------------------------
template <typename SampleT>
struct PassThroughSession
{
    void process(SampleT* in, int inCount, SampleT* out, int outCount, float)
    {
        std::memcpy(out, in, std::min(inCount, outCount) * static_cast<int>(sizeof(SampleT)));
    }
};

// ---------------------------------------------------------------------------
// toKrispRate
// ---------------------------------------------------------------------------

static int testToKrispRateValid()
{
    using SR = Krisp::AudioSdk::SamplingRate;
    ASSERT(KrispGst::toKrispRate(8000) == SR::Sr8000Hz);
    ASSERT(KrispGst::toKrispRate(16000) == SR::Sr16000Hz);
    ASSERT(KrispGst::toKrispRate(24000) == SR::Sr24000Hz);
    ASSERT(KrispGst::toKrispRate(32000) == SR::Sr32000Hz);
    ASSERT(KrispGst::toKrispRate(44100) == SR::Sr44100Hz);
    ASSERT(KrispGst::toKrispRate(48000) == SR::Sr48000Hz);
    ASSERT(KrispGst::toKrispRate(88200) == SR::Sr88200Hz);
    ASSERT(KrispGst::toKrispRate(96000) == SR::Sr96000Hz);
    return 0;
}

static int testToKrispRateInvalid()
{
    ASSERT_THROWS(KrispGst::toKrispRate(0));
    ASSERT_THROWS(KrispGst::toKrispRate(-1));
    ASSERT_THROWS(KrispGst::toKrispRate(44000)); // close but not supported
    ASSERT_THROWS(KrispGst::toKrispRate(22050)); // common rate, but not in the list
    return 0;
}

// ---------------------------------------------------------------------------
// toKrispFrameDuration
// ---------------------------------------------------------------------------

static int testToKrispFrameDurationValid()
{
    using FD = Krisp::AudioSdk::FrameDuration;
    ASSERT(KrispGst::toKrispFrameDuration(10) == FD::Fd10ms);
    ASSERT(KrispGst::toKrispFrameDuration(15) == FD::Fd15ms);
    ASSERT(KrispGst::toKrispFrameDuration(20) == FD::Fd20ms);
    ASSERT(KrispGst::toKrispFrameDuration(30) == FD::Fd30ms);
    ASSERT(KrispGst::toKrispFrameDuration(32) == FD::Fd32ms);
    return 0;
}

static int testToKrispFrameDurationInvalid()
{
    ASSERT_THROWS(KrispGst::toKrispFrameDuration(0));
    ASSERT_THROWS(KrispGst::toKrispFrameDuration(11)); // between valid values
    ASSERT_THROWS(KrispGst::toKrispFrameDuration(25)); // between valid values
    ASSERT_THROWS(KrispGst::toKrispFrameDuration(100));
    return 0;
}

// ---------------------------------------------------------------------------
// utf8ToWide
// ---------------------------------------------------------------------------

static int testUtf8ToWideEmpty()
{
    ASSERT(KrispGst::utf8ToWide("") == L"");
    return 0;
}

static int testUtf8ToWideAscii()
{
    const std::wstring result = KrispGst::utf8ToWide("/models/nc.kef");
    ASSERT(result == L"/models/nc.kef");
    return 0;
}

// é = U+00E9 → UTF-8: 0xC3 0xA9
static int testUtf8ToWide2Byte()
{
    const std::wstring result = KrispGst::utf8ToWide("\xC3\xA9");
    ASSERT(result.size() == 1);
    ASSERT(result[0] == static_cast<wchar_t>(0x00E9));
    return 0;
}

// € = U+20AC → UTF-8: 0xE2 0x82 0xAC
static int testUtf8ToWide3Byte()
{
    const std::wstring result = KrispGst::utf8ToWide("\xE2\x82\xAC");
    ASSERT(result.size() == 1);
    ASSERT(result[0] == static_cast<wchar_t>(0x20AC));
    return 0;
}

// 😀 = U+1F600 → UTF-8: 0xF0 0x9F 0x98 0x80
// UTF-32 (Linux/macOS): one wchar_t; UTF-16 (Windows): surrogate pair
static int testUtf8ToWide4Byte()
{
    const std::wstring result = KrispGst::utf8ToWide("\xF0\x9F\x98\x80");
    if constexpr (sizeof(wchar_t) == 4)
    {
        ASSERT(result.size() == 1);
        ASSERT(result[0] == static_cast<wchar_t>(0x1F600));
    }
    else
    {
        ASSERT(result.size() == 2);
        ASSERT(result[0] == static_cast<wchar_t>(0xD83D)); // high surrogate
        ASSERT(result[1] == static_cast<wchar_t>(0xDE00)); // low surrogate
    }
    return 0;
}

// Mixed: ASCII + 2-byte + 3-byte in one path string
static int testUtf8ToWideMixed()
{
    // "/ára/" — á = U+00E1 (0xC3 0xA1)
    const std::wstring result = KrispGst::utf8ToWide("/\xC3\xA1ra/");
    ASSERT(result.size() == 5);
    ASSERT(result[0] == L'/');
    ASSERT(result[1] == static_cast<wchar_t>(0x00E1));
    ASSERT(result[2] == L'r');
    ASSERT(result[3] == L'a');
    ASSERT(result[4] == L'/');
    return 0;
}

static int testUtf8ToWideInvalidLeadByte()
{
    ASSERT_THROWS(KrispGst::utf8ToWide("\xFF"));
    ASSERT_THROWS(KrispGst::utf8ToWide("\xFE"));
    return 0;
}

static int testUtf8ToWideTruncatedSequence()
{
    ASSERT_THROWS(KrispGst::utf8ToWide("\xC3"));         // 2-byte, missing continuation
    ASSERT_THROWS(KrispGst::utf8ToWide("\xE2\x82"));     // 3-byte, missing last byte
    ASSERT_THROWS(KrispGst::utf8ToWide("\xF0\x9F\x98")); // 4-byte, missing last byte
    return 0;
}

static int testUtf8ToWideInvalidContinuation()
{
    ASSERT_THROWS(KrispGst::utf8ToWide("\xC3\x20")); // continuation byte expected, got ASCII
    return 0;
}

// ---------------------------------------------------------------------------
// KrispSession — construction validation
// ---------------------------------------------------------------------------

static int testKrispSessionZeroFrameSzThrows()
{
    using S = KrispGst::KrispSession<PassThroughSession<float>, float>;
    ASSERT_THROWS(S(std::make_shared<PassThroughSession<float>>(), 0));
    return 0;
}

static int testKrispSessionNegativeFrameSzThrows()
{
    using S = KrispGst::KrispSession<PassThroughSession<float>, float>;
    ASSERT_THROWS(S(std::make_shared<PassThroughSession<float>>(), -1));
    return 0;
}

// ---------------------------------------------------------------------------
// KrispSession FIFO — float
// ---------------------------------------------------------------------------

using FloatSession = KrispGst::KrispSession<PassThroughSession<float>, float>;

static FloatSession makeFloatSession(int frameSz)
{
    return FloatSession(std::make_shared<PassThroughSession<float>>(), frameSz);
}

static std::vector<float> process(FloatSession& s, std::vector<float> buf)
{
    s.processInplace(buf.data(), buf.size(), 100.0f);
    return buf;
}

// Exactly one frame in → one frame out (copy-through)
static int testFifoExactFrame()
{
    const int frameSz = 16;
    auto s = makeFloatSession(frameSz);

    std::vector<float> input(frameSz);
    for (int i = 0; i < frameSz; ++i)
    {
        input[i] = static_cast<float>(i + 1);
    }

    const auto out = process(s, input);
    for (int i = 0; i < frameSz; ++i)
    {
        ASSERT(out[i] == input[i]);
    }
    return 0;
}

// Less than one frame in → output is zero-filled (startup latency)
static int testFifoSubFrameZeroFill()
{
    const int frameSz = 16;
    auto s = makeFloatSession(frameSz);

    const auto out = process(s, std::vector<float>(8, 1.0f));
    for (float v : out)
    {
        ASSERT(v == 0.0f);
    }
    return 0;
}

// Two complete frames in → both processed and returned in order
static int testFifoTwoFrames()
{
    const int frameSz = 8;
    auto s = makeFloatSession(frameSz);

    std::vector<float> input(frameSz * 2);
    for (int i = 0; i < frameSz * 2; ++i)
    {
        input[i] = static_cast<float>(i + 1);
    }

    const auto out = process(s, input);
    for (int i = 0; i < frameSz * 2; ++i)
    {
        ASSERT(out[i] == input[i]);
    }
    return 0;
}

// Two half-frame calls accumulate into one complete frame on the second call
static int testFifoAccumulation()
{
    const int frameSz = 16;
    auto s = makeFloatSession(frameSz);

    // First 8 samples — not enough to process a frame
    std::vector<float> first(8);
    for (int i = 0; i < 8; ++i)
    {
        first[i] = static_cast<float>(i + 1);
    }
    const auto out1 = process(s, first);
    for (float v : out1)
    {
        ASSERT(v == 0.0f); // zero-filled: no complete frame yet
    }

    // Next 8 samples complete the frame; output drains its first 8
    std::vector<float> second(8);
    for (int i = 0; i < 8; ++i)
    {
        second[i] = static_cast<float>(i + 9);
    }
    const auto out2 = process(s, second);
    for (int i = 0; i < 8; ++i)
    {
        ASSERT(out2[i] == static_cast<float>(i + 1)); // first half of processed frame
    }
    return 0;
}

// 1.5 frames in → first frame returned, 0.5-frame tail in output carry zero-fills
static int testFifoPartialTail()
{
    const int frameSz = 10;
    auto s = makeFloatSession(frameSz);

    std::vector<float> input(15);
    for (int i = 0; i < 15; ++i)
    {
        input[i] = static_cast<float>(i + 1);
    }

    const auto out = process(s, input);

    // First 10 samples: processed frame matches input
    for (int i = 0; i < 10; ++i)
    {
        ASSERT(out[i] == input[i]);
    }
    // Remaining 5: zero-filled (unprocessed carry waits for next call)
    for (int i = 10; i < 15; ++i)
    {
        ASSERT(out[i] == 0.0f);
    }
    return 0;
}

// Output carry drains across consecutive calls
static int testFifoOutputCarryDrain()
{
    const int frameSz = 8;
    auto s = makeFloatSession(frameSz);

    // Feed 2 frames at once — output carry will have 8 samples left after draining first call
    std::vector<float> input(frameSz * 2);
    for (int i = 0; i < frameSz * 2; ++i)
    {
        input[i] = static_cast<float>(i + 1);
    }
    const auto out1 = process(s, input); // drains 16 samples, carry empty

    // Feed one more frame
    std::vector<float> next(frameSz);
    for (int i = 0; i < frameSz; ++i)
    {
        next[i] = static_cast<float>(i + 100);
    }
    const auto out2 = process(s, next);
    for (int i = 0; i < frameSz; ++i)
    {
        ASSERT(out2[i] == next[i]);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// KrispSession FIFO — int16_t (type-independent logic)
// ---------------------------------------------------------------------------

static int testFifoInt16ExactFrame()
{
    using Session16 = KrispGst::KrispSession<PassThroughSession<int16_t>, int16_t>;
    const int frameSz = 8;
    Session16 s(std::make_shared<PassThroughSession<int16_t>>(), frameSz);

    std::vector<int16_t> input(frameSz);
    for (int i = 0; i < frameSz; ++i)
    {
        input[i] = static_cast<int16_t>(i * 1000);
    }

    std::vector<int16_t> buf(input);
    s.processInplace(buf.data(), buf.size(), 100.0f);

    for (int i = 0; i < frameSz; ++i)
    {
        ASSERT(buf[i] == input[i]);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Test runner
// ---------------------------------------------------------------------------

struct Test
{
    const char* name;
    int (*fn)();
};

static const Test kTests[] = {
    {"toKrispRate: all 8 valid rates", testToKrispRateValid},
    {"toKrispRate: unsupported rates throw", testToKrispRateInvalid},
    {"toKrispFrameDuration: all 5 valid values", testToKrispFrameDurationValid},
    {"toKrispFrameDuration: invalid values throw", testToKrispFrameDurationInvalid},
    {"utf8ToWide: empty string", testUtf8ToWideEmpty},
    {"utf8ToWide: pure ASCII", testUtf8ToWideAscii},
    {"utf8ToWide: 2-byte sequence (U+00E9)", testUtf8ToWide2Byte},
    {"utf8ToWide: 3-byte sequence (U+20AC)", testUtf8ToWide3Byte},
    {"utf8ToWide: 4-byte sequence (U+1F600)", testUtf8ToWide4Byte},
    {"utf8ToWide: mixed ASCII and multibyte", testUtf8ToWideMixed},
    {"utf8ToWide: invalid lead byte throws", testUtf8ToWideInvalidLeadByte},
    {"utf8ToWide: truncated sequence throws", testUtf8ToWideTruncatedSequence},
    {"utf8ToWide: invalid continuation byte throws", testUtf8ToWideInvalidContinuation},
    {"KrispSession: zero frameSz throws", testKrispSessionZeroFrameSzThrows},
    {"KrispSession: negative frameSz throws", testKrispSessionNegativeFrameSzThrows},
    {"FIFO float: exact frame", testFifoExactFrame},
    {"FIFO float: sub-frame zero-fill", testFifoSubFrameZeroFill},
    {"FIFO float: two full frames", testFifoTwoFrames},
    {"FIFO float: accumulation across calls", testFifoAccumulation},
    {"FIFO float: partial tail zero-fill", testFifoPartialTail},
    {"FIFO float: output carry drains correctly", testFifoOutputCarryDrain},
    {"FIFO int16: exact frame", testFifoInt16ExactFrame},
};

int main()
{
    int passed = 0;
    int failed = 0;

    std::printf("=== Krisp unit tests ===\n\n");

    for (const auto& t : kTests)
    {
        const int r = t.fn();
        if (r == 0)
        {
            std::printf("  PASS: %s\n", t.name);
            ++passed;
        }
        else
        {
            std::printf("  FAIL: %s\n", t.name);
            ++failed;
        }
    }

    std::printf("\n%d passed, %d failed\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
