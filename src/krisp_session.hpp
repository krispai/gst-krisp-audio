///
/// Copyright Krisp, Inc
///
/// Shared C++ helper that owns a Krisp NC/VI or AR session and manages the
/// process-wide globalInit/Destroy lifecycle (ref-counted).
///
/// Processing strategy: carry-buffer FIFO.
/// Incoming samples are appended to _inCarry. Complete frames are
/// Krisp-processed into _outCarry. Output is drained from _outCarry back
/// into the GstBuffer. This handles arbitrary upstream buffer sizes without
/// dropping samples or breaking the SDK's internal state. The only cost is
/// up to one frame (<= frameDurationMs) of startup latency.
///
#pragma once

#include <krisp-audio-api-definitions.hpp>
#include <krisp-audio-sdk.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Process-wide globalInit / globalDestroy reference counter
// ---------------------------------------------------------------------------
namespace KrispGst
{

class GlobalInit
{
public:
    /// Called once per element instance from setup() — after the license-key
    /// property has been set.  Only the first call (refcount 0→1) invokes
    /// globalInit; subsequent callers just increment the ref-count.
    ///
    /// @param licenseKey  Krisp SDK license key.
    /// @param logCallback Optional callback forwarded to the SDK for log messages.
    ///                    Called on the SDK's internal thread; must be thread-safe.
    ///                    Only the first caller's callback is used (subsequent
    ///                    acquire() calls are no-ops since the SDK is already up).
    /// @param logLevel    Minimum SDK log level passed to globalInit (default: Trace,
    ///                    actual output is filtered by the GStreamer debug category).
    static void acquire(
        const std::string& licenseKey,
        Krisp::AudioSdk::LogCallback logCallback = nullptr,
        Krisp::AudioSdk::LogLevel logLevel = Krisp::AudioSdk::LogLevel::Trace)
    {
        std::lock_guard<std::mutex> lk(_mutex);
        if (_refcount > 0)
        {
            ++_refcount;
            return; // SDK already initialised by another element instance
        }

        {
            std::lock_guard<std::mutex> elk(_errorMutex);
            _lastError.clear();
        }

        // Increment only after globalInit succeeds. If globalInit throws, _refcount
        // stays at 0 so the next acquire() retries initialization rather than
        // treating the SDK as already up.
        Krisp::AudioSdk::globalInit(
            L"",
            licenseKey,
            [](Krisp::AudioSdk::LicensingError err, const std::string& msg)
            {
                if (err != Krisp::AudioSdk::LicensingError::Success)
                {
                    std::lock_guard<std::mutex> lk(_errorMutex);
                    _lastError = msg;
                }
            },
            logCallback,
            logLevel);
        ++_refcount;
    }

    static void release()
    {
        std::lock_guard<std::mutex> lk(_mutex);
        assert(_refcount > 0 && "release() called without a matching acquire()");
        if (_refcount == 0)
        {
            return; // prevent underflow in release builds
        }
        if (--_refcount == 0)
        {
            Krisp::AudioSdk::globalDestroy();
        }
    }

    /// Returns the most recent licensing error message set by the async
    /// callback, or an empty string if no error has occurred.
    static std::string lastLicensingError()
    {
        std::lock_guard<std::mutex> lk(_errorMutex);
        return _lastError;
    }

private:
    static std::mutex _mutex;      // guards _refcount
    static std::mutex _errorMutex; // guards _lastError (used by async callback)
    static int _refcount;
    static std::string _lastError;
};

// ---------------------------------------------------------------------------
// Enum conversion helpers
// ---------------------------------------------------------------------------

inline Krisp::AudioSdk::SamplingRate toKrispRate(int rate)
{
    using SR = Krisp::AudioSdk::SamplingRate;
    switch (rate)
    {
        case 8000:
            return SR::Sr8000Hz;
        case 16000:
            return SR::Sr16000Hz;
        case 24000:
            return SR::Sr24000Hz;
        case 32000:
            return SR::Sr32000Hz;
        case 44100:
            return SR::Sr44100Hz;
        case 48000:
            return SR::Sr48000Hz;
        case 88200:
            return SR::Sr88200Hz;
        case 96000:
            return SR::Sr96000Hz;
        default:
            throw std::invalid_argument("Unsupported sample rate: " + std::to_string(rate));
    }
}

inline Krisp::AudioSdk::FrameDuration toKrispFrameDuration(int ms)
{
    using FD = Krisp::AudioSdk::FrameDuration;
    switch (ms)
    {
        case 10:
            return FD::Fd10ms;
        case 15:
            return FD::Fd15ms;
        case 20:
            return FD::Fd20ms;
        case 30:
            return FD::Fd30ms;
        case 32:
            return FD::Fd32ms;
        default:
            throw std::invalid_argument("Unsupported frame duration: " + std::to_string(ms) + "ms");
    }
}

// ---------------------------------------------------------------------------
// UTF-8 → wide string (no locale dependency, no <filesystem>)
//
// Handles the full Unicode range.  On Linux/macOS wchar_t is 32-bit (UTF-32);
// on Windows it is 16-bit (UTF-16), so surrogate pairs are emitted there.
// Throws std::invalid_argument if the input is not valid UTF-8.
// ---------------------------------------------------------------------------
inline std::wstring utf8ToWide(const std::string& utf8)
{
    std::wstring result;
    result.reserve(utf8.size());

    for (std::size_t i = 0; i < utf8.size();)
    {
        uint32_t cp = 0;
        const auto c = static_cast<unsigned char>(utf8[i]);
        int extra = 0;

        if (c < 0x80)
        {
            cp = c;
            extra = 0;
        }
        else if ((c & 0xE0) == 0xC0)
        {
            cp = c & 0x1Fu;
            extra = 1;
        }
        else if ((c & 0xF0) == 0xE0)
        {
            cp = c & 0x0Fu;
            extra = 2;
        }
        else if ((c & 0xF8) == 0xF0)
        {
            cp = c & 0x07u;
            extra = 3;
        }
        else
        {
            throw std::invalid_argument("Invalid UTF-8 sequence in model path");
        }

        ++i;
        for (int j = 0; j < extra; ++j, ++i)
        {
            if (i >= utf8.size() || (static_cast<unsigned char>(utf8[i]) & 0xC0) != 0x80)
            {
                throw std::invalid_argument("Invalid UTF-8 sequence in model path");
            }
            cp = (cp << 6) | (static_cast<unsigned char>(utf8[i]) & 0x3Fu);
        }

        if constexpr (sizeof(wchar_t) == 2) // UTF-16 (Windows)
        {
            if (cp < 0x10000u)
            {
                result.push_back(static_cast<wchar_t>(cp));
            }
            else
            {
                cp -= 0x10000u;
                result.push_back(static_cast<wchar_t>(0xD800u | (cp >> 10)));
                result.push_back(static_cast<wchar_t>(0xDC00u | (cp & 0x3FFu)));
            }
        }
        else // UTF-32 (Linux / macOS)
        {
            result.push_back(static_cast<wchar_t>(cp));
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// Base interface
// ---------------------------------------------------------------------------
class IKrispSession
{
public:
    virtual ~IKrispSession() = default;

    /// Process @p numSamples in-place.
    /// Complete frames from the start are Krisp-processed; the trailing
    /// partial frame is left untouched.
    virtual void processInplace(void* data, std::size_t numSamples, float nsLevel) = 0;

    /// Bytes per sample for the negotiated format.
    virtual int bytesPerSample() const = 0;

    /// Samples per Krisp frame.
    virtual int frameSize() const = 0;
};

// ---------------------------------------------------------------------------
// Concrete session template
// ---------------------------------------------------------------------------
template <typename SessionT, typename SampleT>
class KrispSession final : public IKrispSession
{
public:
    KrispSession(std::shared_ptr<SessionT> session, int frameSz) : _session(std::move(session)), _frameSz(frameSz)
    {
        if (frameSz <= 0)
        {
            throw std::invalid_argument("frameSz must be > 0, got " + std::to_string(frameSz));
        }
        _temp.resize(frameSz);
    }

    int bytesPerSample() const override
    {
        return sizeof(SampleT);
    }
    int frameSize() const override
    {
        return _frameSz;
    }

    void processInplace(void* data, std::size_t numSamples, float nsLevel) override
    {
        auto* buf = static_cast<SampleT*>(data);

        // Accumulate incoming samples in the input carry
        _inCarry.insert(_inCarry.end(), buf, buf + numSamples);

        // Process every complete frame available in the input carry
        std::size_t pos = 0;
        while (pos + static_cast<std::size_t>(_frameSz) <= _inCarry.size())
        {
            _session->process(_inCarry.data() + pos, _frameSz, _temp.data(), _frameSz, nsLevel);
            _outCarry.insert(_outCarry.end(), _temp.begin(), _temp.end());
            pos += _frameSz;
        }

        // Remove the consumed input; tail (< _frameSz) stays for the next call
        if (pos > 0)
        {
            _inCarry.erase(_inCarry.begin(), _inCarry.begin() + pos);
        }

        // Drain processed output into the GstBuffer
        std::size_t available = std::min(numSamples, _outCarry.size());
        if (available > 0)
        {
            std::memcpy(buf, _outCarry.data(), available * sizeof(SampleT));
            _outCarry.erase(_outCarry.begin(), _outCarry.begin() + available);
        }

        // Startup only: output FIFO not yet primed — zero-fill the remainder
        if (available < numSamples)
        {
            std::memset(buf + available, 0, (numSamples - available) * sizeof(SampleT));
        }
    }

private:
    std::shared_ptr<SessionT> _session;
    int _frameSz;
    std::vector<SampleT> _temp;     // SDK output scratch (one frame)
    std::vector<SampleT> _inCarry;  // input samples awaiting a complete frame
    std::vector<SampleT> _outCarry; // processed output awaiting consumption
};

} // namespace KrispGst
