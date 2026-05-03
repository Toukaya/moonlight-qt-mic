#include "microphonecapture.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <Limelight.h>

MicrophoneCapture::MicrophoneCapture(QObject* parent)
    : QObject(parent)
    , m_DeviceId(0)
    , m_ObtainedSpec({})
    , m_Encoder(nullptr)
    , m_Streaming(false)
    , m_StopEncoderThread(false)
    , m_Initialized(false)
    , m_Enabled(false)
    , m_FirstPacketLogged(false)
    , m_OpusRate(48000)
    , m_OpusChannels(1)
    , m_FrameSize(960)
    , m_PcmDumpFile(nullptr)
    , m_PcmDumpDataBytes(0)
    , m_StatsFrameCounter(0)
    , m_StatsTrimCount(0)
    , m_StatsBufferHighWater(0)
    , m_StatsPeakAbs(0)
    , m_StatsBytesEncoded(0)
    , m_StatsPacketsEncoded(0)
{
}

namespace {

// Opus only accepts these input rates. Any other native rate forces a
// resample step on the way in.
bool isOpusRate(int rate)
{
    return rate == 8000 || rate == 12000 || rate == 16000 ||
           rate == 24000 || rate == 48000;
}

} // namespace

MicrophoneCapture::~MicrophoneCapture()
{
    stop();

    m_StopEncoderThread.store(true, std::memory_order_release);
    m_BufferCondition.notify_all();
    if (m_EncoderThread.joinable()) {
        m_EncoderThread.join();
    }

    if (m_DeviceId != 0) {
        SDL_CloseAudioDevice(m_DeviceId);
        m_DeviceId = 0;
    }

    if (m_Encoder != nullptr) {
        opus_encoder_destroy(m_Encoder);
        m_Encoder = nullptr;
    }

    closePcmDump();
}

bool MicrophoneCapture::initialize(const std::string& deviceName)
{
    if (m_Initialized) {
        return true;
    }

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "SDL_InitSubSystem(SDL_INIT_AUDIO) failed for microphone capture: %s",
                    SDL_GetError());
        return false;
    }

    // Use the highest-quality SDL resampler that's compiled in. With brew's
    // SDL2 (no libsamplerate) this falls back to linear interpolation; the
    // hint is harmless and helps if SDL is ever rebuilt with libsamplerate.
    SDL_SetHintWithPriority(SDL_HINT_AUDIO_RESAMPLING_MODE, "best", SDL_HINT_OVERRIDE);

    const char* selectedDevice = deviceName.empty() ? nullptr : deviceName.c_str();

    // Phase 1: probe the device's preferred config so we can pair Opus to it
    // and skip an SDL-internal resample when possible.
    SDL_AudioSpec probeDesired = {};
    probeDesired.freq = 48000;
    probeDesired.format = AUDIO_S16SYS;
    probeDesired.channels = 1;
    probeDesired.samples = 960;
    probeDesired.callback = &MicrophoneCapture::audioCallback;
    probeDesired.userdata = this;
    SDL_AudioSpec probed = {};
    const int probeAllowed = SDL_AUDIO_ALLOW_FREQUENCY_CHANGE |
                             SDL_AUDIO_ALLOW_CHANNELS_CHANGE |
                             SDL_AUDIO_ALLOW_SAMPLES_CHANGE;
    SDL_AudioDeviceID probeId = SDL_OpenAudioDevice(selectedDevice, 1,
                                                    &probeDesired, &probed,
                                                    probeAllowed);
    if (probeId == 0 && selectedDevice != nullptr) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Probe of requested microphone '%s' failed: %s. Falling back to default input device.",
                    selectedDevice, SDL_GetError());
        selectedDevice = nullptr;
        probeId = SDL_OpenAudioDevice(nullptr, 1, &probeDesired, &probed, probeAllowed);
    }
    if (probeId == 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "SDL_OpenAudioDevice() probe failed for microphone capture: %s",
                    SDL_GetError());
        return false;
    }
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Microphone native config: %d Hz, %d channels, format 0x%x, %d samples/cb",
                probed.freq, probed.channels, probed.format, probed.samples);
    SDL_CloseAudioDevice(probeId);

    // Phase 2: pick an Opus rate. If the device is already at an Opus-supported
    // rate, encode at that rate so SDL inserts no resampler. Otherwise target
    // 48 kHz and accept SDL's internal (linear) resample.
    int opusRate;
    int allowFreqChange;
    if (isOpusRate(probed.freq)) {
        opusRate = probed.freq;
        allowFreqChange = SDL_AUDIO_ALLOW_FREQUENCY_CHANGE;
    } else {
        opusRate = 48000;
        allowFreqChange = 0;
    }

    // Opus only handles mono or stereo. Anything more (rare) gets downmixed
    // by SDL via ALLOW_CHANNELS_CHANGE; the obtained spec must match what
    // we asked.
    int opusChannels = (probed.channels >= 2) ? 2 : 1;

    SDL_AudioSpec desired = {};
    desired.freq = opusRate;
    desired.format = AUDIO_S16SYS;
    desired.channels = static_cast<Uint8>(opusChannels);
    desired.samples = static_cast<Uint16>((opusRate * 20) / 1000);
    desired.callback = &MicrophoneCapture::audioCallback;
    desired.userdata = this;

    const int allowedChanges = allowFreqChange | SDL_AUDIO_ALLOW_SAMPLES_CHANGE;
    m_DeviceId = SDL_OpenAudioDevice(selectedDevice, 1, &desired, &m_ObtainedSpec, allowedChanges);
    if (m_DeviceId == 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "SDL_OpenAudioDevice() failed for microphone capture: %s",
                    SDL_GetError());
        return false;
    }

    if (m_ObtainedSpec.format != AUDIO_S16SYS ||
            m_ObtainedSpec.freq != opusRate ||
            m_ObtainedSpec.channels != opusChannels) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Microphone capture format mismatch: got %d Hz, %d channels, format 0x%x; wanted %d Hz, %d channels",
                    m_ObtainedSpec.freq, m_ObtainedSpec.channels, m_ObtainedSpec.format,
                    opusRate, opusChannels);
        SDL_CloseAudioDevice(m_DeviceId);
        m_DeviceId = 0;
        return false;
    }

    m_OpusRate = opusRate;
    m_OpusChannels = opusChannels;
    m_FrameSize = (m_OpusRate * 20) / 1000;

    int opusError = OPUS_OK;
    m_Encoder = opus_encoder_create(m_OpusRate, m_OpusChannels,
                                    OPUS_APPLICATION_AUDIO, &opusError);
    if (m_Encoder == nullptr || opusError != OPUS_OK) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "opus_encoder_create() failed for microphone capture: %s",
                    opus_strerror(opusError));
        SDL_CloseAudioDevice(m_DeviceId);
        m_DeviceId = 0;
        return false;
    }

    // Bitrate scales with channel count. 96 kbps mono is high-quality voice;
    // 160 kbps stereo gives equivalent per-channel headroom. Both leave plenty
    // of room for fullband (20 kHz) content.
    const int bitrate = (m_OpusChannels == 2) ? 160000 : 96000;

    opus_encoder_ctl(m_Encoder, OPUS_SET_BITRATE(bitrate));
    opus_encoder_ctl(m_Encoder, OPUS_SET_VBR(1));
    opus_encoder_ctl(m_Encoder, OPUS_SET_COMPLEXITY(10));
    // OPUS_AUTO lets the encoder pick speech-vs-music heuristics per frame
    // instead of the previous OPUS_SIGNAL_VOICE lock-in (which combined with
    // OPUS_APPLICATION_VOIP was double-biased toward narrow speech mode).
    opus_encoder_ctl(m_Encoder, OPUS_SET_SIGNAL(OPUS_AUTO));
    // Without this, Opus is free to downgrade to wideband (8 kHz cutoff) at
    // lower bitrates, which is the most likely cause of the muffled-mic
    // complaint on the previous setup.
    opus_encoder_ctl(m_Encoder, OPUS_SET_MAX_BANDWIDTH(OPUS_BANDWIDTH_FULLBAND));
    opus_encoder_ctl(m_Encoder, OPUS_SET_LSB_DEPTH(16));
    opus_encoder_ctl(m_Encoder, OPUS_SET_DTX(0));
    // FEC + a non-zero PACKET_LOSS_PERC tax the audio bit budget for
    // redundancy. On a Thunderbolt direct link there's no loss to recover
    // from, so we keep the bits for fidelity.
    opus_encoder_ctl(m_Encoder, OPUS_SET_INBAND_FEC(0));
    opus_encoder_ctl(m_Encoder, OPUS_SET_PACKET_LOSS_PERC(0));
    opus_encoder_ctl(m_Encoder, OPUS_SET_EXPERT_FRAME_DURATION(OPUS_FRAMESIZE_20_MS));

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Microphone capture: device='%s'; SDL %d Hz / %d ch / %d samples per cb; "
                "Opus %d Hz / %d ch / %d kbps fullband, FEC off, no preemptive loss; resample=%s",
                selectedDevice ? selectedDevice : "<default>",
                m_ObtainedSpec.freq, m_ObtainedSpec.channels, m_ObtainedSpec.samples,
                m_OpusRate, m_OpusChannels, bitrate / 1000,
                (probed.freq == m_OpusRate) ? "none" : "SDL");

    SDL_PauseAudioDevice(m_DeviceId, 1);
    m_SampleBuffer.reserve(static_cast<size_t>(m_FrameSize) * m_OpusChannels * 4);
    openPcmDump();
    m_StopEncoderThread.store(false, std::memory_order_release);
    m_EncoderThread = std::thread(&MicrophoneCapture::encoderLoop, this);
    m_Initialized = true;
    return true;
}

bool MicrophoneCapture::start()
{
    if (!m_Enabled || !m_Initialized || m_DeviceId == 0 || !LiIsMicrophoneStreamActive()) {
        return false;
    }

    clearBufferedSamples();
    m_FirstPacketLogged = false;
    m_Streaming.store(true, std::memory_order_release);
    m_BufferCondition.notify_all();
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Microphone capture streaming started; negotiated mic stream is active");
    SDL_PauseAudioDevice(m_DeviceId, 0);
    return true;
}

void MicrophoneCapture::stop()
{
    if (m_DeviceId != 0) {
        SDL_PauseAudioDevice(m_DeviceId, 1);
    }

    m_Streaming.store(false, std::memory_order_release);
    clearBufferedSamples();
    m_BufferCondition.notify_all();
}

void MicrophoneCapture::setEnabled(bool enabled)
{
    m_Enabled = enabled;
    if (!m_Enabled) {
        stop();
    }
}

bool MicrophoneCapture::isEnabled() const
{
    return m_Enabled;
}

bool MicrophoneCapture::isStreaming() const
{
    return m_Streaming.load(std::memory_order_acquire);
}

void MicrophoneCapture::audioCallback(void* userdata, Uint8* stream, int len)
{
    auto* capture = static_cast<MicrophoneCapture*>(userdata);
    if (capture != nullptr) {
        capture->handleAudioData(stream, len);
    }
}

void MicrophoneCapture::handleAudioData(const Uint8* stream, int len)
{
    if (!m_Streaming.load(std::memory_order_acquire) || stream == nullptr || len <= 0) {
        return;
    }

    const auto* inputSamples = reinterpret_cast<const opus_int16*>(stream);
    const int valueCount = len / (int)sizeof(opus_int16);

    // Diagnostic: write the post-SDL samples to a WAV file (if requested via
    // env var). Doing this BEFORE the buffer/trim path means the WAV is what
    // SDL gave us, untouched by anything we do downstream.
    writePcmDump(inputSamples, valueCount);

    {
        std::lock_guard lock(m_BufferMutex);
        m_SampleBuffer.insert(m_SampleBuffer.end(), inputSamples, inputSamples + valueCount);
        if (m_SampleBuffer.size() > m_StatsBufferHighWater) {
            m_StatsBufferHighWater = m_SampleBuffer.size();
        }
        // ~240 ms ceiling: drop the OLDEST samples once we exceed it. Buffer
        // values are interleaved across channels, so the cap scales with
        // m_OpusChannels.
        const size_t maxBufferedValues = static_cast<size_t>(m_FrameSize) * m_OpusChannels * 12;
        if (m_SampleBuffer.size() > maxBufferedValues) {
            const auto trimValues = m_SampleBuffer.size() - maxBufferedValues;
            m_SampleBuffer.erase(m_SampleBuffer.begin(), m_SampleBuffer.begin() + trimValues);
            m_StatsTrimCount++;
        }
    }
    m_BufferCondition.notify_one();
}

void MicrophoneCapture::encoderLoop()
{
    // m_FrameSize is samples-per-channel; m_OpusChannels is 1 or 2. The
    // working buffer is interleaved (L,R,L,R,...) for stereo.
    const int frameValues = m_FrameSize * m_OpusChannels;
    std::vector<opus_int16> frame(frameValues);
    const auto frameDuration = std::chrono::milliseconds((m_FrameSize * 1000) / m_OpusRate);
    auto nextSendDeadline = std::chrono::steady_clock::now();
    bool pacingActive = false;

    for (;;) {
        {
            std::unique_lock lock(m_BufferMutex);
            m_BufferCondition.wait(lock, [this, frameValues] {
                return m_StopEncoderThread.load(std::memory_order_acquire) ||
                       (m_Streaming.load(std::memory_order_acquire) &&
                        m_SampleBuffer.size() >= (size_t)frameValues);
            });

            if (m_StopEncoderThread.load(std::memory_order_acquire)) {
                break;
            }

            if (!m_Streaming.load(std::memory_order_acquire) ||
                    m_SampleBuffer.size() < (size_t)frameValues) {
                pacingActive = false;
                continue;
            }

            std::copy_n(m_SampleBuffer.begin(), frameValues, frame.begin());
            m_SampleBuffer.erase(m_SampleBuffer.begin(), m_SampleBuffer.begin() + frameValues);
        }

        const auto now = std::chrono::steady_clock::now();
        if (!pacingActive) {
            nextSendDeadline = now;
            pacingActive = true;
        }
        else if (now > nextSendDeadline + (frameDuration * 2)) {
            // Re-sync the pacing clock after a long capture gap to avoid compounding stale latency.
            nextSendDeadline = now;
        }

        if (nextSendDeadline > now) {
            std::this_thread::sleep_until(nextSendDeadline);
        }

        nextSendDeadline += frameDuration;

        // Track input peak amplitude for the stats line below.
        for (int i = 0; i < frameValues; i++) {
            opus_int16 s = frame[i];
            opus_int16 a = (s < 0) ? (opus_int16)(-(s + 1)) : s; // avoid INT16_MIN overflow
            if (a > m_StatsPeakAbs) {
                m_StatsPeakAbs = a;
            }
        }

        int encodedBytes = opus_encode(m_Encoder,
                                       frame.data(),
                                       m_FrameSize,
                                       m_EncodedPacket.data(),
                                       (opus_int32)m_EncodedPacket.size());
        if (encodedBytes <= 0) {
            continue;
        }

        int sendResult = LiSendMicrophoneOpusDataEx(m_EncodedPacket.data(), encodedBytes, m_FrameSize);
        if (sendResult >= 0 && !m_FirstPacketLogged) {
            m_FirstPacketLogged = true;
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "Sent first client microphone packet (%d bytes Opus)",
                        encodedBytes);
        }
        else if (sendResult < 0) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "LiSendMicrophoneOpusDataEx() failed for microphone capture");
        }

        m_StatsBytesEncoded += encodedBytes;
        m_StatsPacketsEncoded++;
        m_StatsFrameCounter++;

        // Emit a stats line every ~1 s (50 frames at 20 ms each), so we can
        // see whether the encoder is starving, overrunning, clipping, etc.
        const int framesPerSec = 1000 / std::max(1, (m_FrameSize * 1000) / m_OpusRate);
        if (m_StatsFrameCounter >= framesPerSec) {
            const double peakDbfs = (m_StatsPeakAbs > 0)
                ? 20.0 * std::log10(static_cast<double>(m_StatsPeakAbs) / 32767.0)
                : -120.0;
            const double avgKbps = (m_StatsPacketsEncoded > 0)
                ? (static_cast<double>(m_StatsBytesEncoded) * 8.0 / 1000.0)
                : 0.0;
            const std::size_t bufferHwMs = (m_StatsBufferHighWater * 1000)
                / (static_cast<std::size_t>(m_OpusRate) * m_OpusChannels);
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "Mic stats[1s]: peak=%.1fdBFS, encoded=%d pkts (%.0f kbps avg), buf-hw=%zu values (%zu ms), trims=%d",
                        peakDbfs,
                        m_StatsPacketsEncoded,
                        avgKbps,
                        m_StatsBufferHighWater,
                        bufferHwMs,
                        m_StatsTrimCount);
            m_StatsFrameCounter = 0;
            m_StatsTrimCount = 0;
            m_StatsBufferHighWater = 0;
            m_StatsPeakAbs = 0;
            m_StatsBytesEncoded = 0;
            m_StatsPacketsEncoded = 0;
        }
    }
}

void MicrophoneCapture::clearBufferedSamples()
{
    {
        std::lock_guard lock(m_BufferMutex);
        m_SampleBuffer.clear();
    }
}

namespace {

// Pack a little-endian uint32 / uint16 into a buffer.
void leU32(unsigned char* p, std::uint32_t v) {
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
}
void leU16(unsigned char* p, std::uint16_t v) {
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
}

} // namespace

void MicrophoneCapture::openPcmDump()
{
    const char* path = std::getenv("MOONLIGHT_MIC_DUMP_PCM");
    if (path == nullptr || *path == '\0') {
        return;
    }
    m_PcmDumpFile = std::fopen(path, "wb");
    if (m_PcmDumpFile == nullptr) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "MOONLIGHT_MIC_DUMP_PCM is set but fopen('%s') failed", path);
        return;
    }

    // Write a 44-byte WAV header with placeholder sizes; we patch in the
    // real sizes in closePcmDump(). Format: PCM 16-bit, m_OpusRate, m_OpusChannels.
    unsigned char hdr[44];
    std::memcpy(hdr, "RIFF", 4);
    leU32(hdr + 4, 0); // RIFF size — filled in on close
    std::memcpy(hdr + 8, "WAVEfmt ", 8);
    leU32(hdr + 16, 16);          // fmt chunk size
    leU16(hdr + 20, 1);           // PCM
    leU16(hdr + 22, (std::uint16_t)m_OpusChannels);
    leU32(hdr + 24, (std::uint32_t)m_OpusRate);
    const std::uint32_t byteRate = (std::uint32_t)m_OpusRate * (std::uint32_t)m_OpusChannels * 2;
    leU32(hdr + 28, byteRate);
    leU16(hdr + 32, (std::uint16_t)(m_OpusChannels * 2)); // block align
    leU16(hdr + 34, 16);          // bits per sample
    std::memcpy(hdr + 36, "data", 4);
    leU32(hdr + 40, 0);           // data size — filled in on close
    std::fwrite(hdr, 1, sizeof(hdr), m_PcmDumpFile);
    m_PcmDumpDataBytes = 0;
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Mic PCM dump: writing post-SDL samples to '%s' (%d Hz, %d ch, S16LE)",
                path, m_OpusRate, m_OpusChannels);
}

void MicrophoneCapture::writePcmDump(const opus_int16* samples, int valueCount)
{
    if (m_PcmDumpFile == nullptr || valueCount <= 0) {
        return;
    }
    const std::size_t bytes = static_cast<std::size_t>(valueCount) * sizeof(opus_int16);
    std::fwrite(samples, 1, bytes, m_PcmDumpFile);
    m_PcmDumpDataBytes += bytes;
}

void MicrophoneCapture::closePcmDump()
{
    if (m_PcmDumpFile == nullptr) {
        return;
    }
    // Patch the placeholder sizes in the WAV header so the file is valid.
    const std::uint32_t dataSize = (std::uint32_t)m_PcmDumpDataBytes;
    const std::uint32_t riffSize = dataSize + 36;
    unsigned char buf[4];
    if (std::fseek(m_PcmDumpFile, 4, SEEK_SET) == 0) {
        leU32(buf, riffSize);
        std::fwrite(buf, 1, 4, m_PcmDumpFile);
    }
    if (std::fseek(m_PcmDumpFile, 40, SEEK_SET) == 0) {
        leU32(buf, dataSize);
        std::fwrite(buf, 1, 4, m_PcmDumpFile);
    }
    std::fclose(m_PcmDumpFile);
    m_PcmDumpFile = nullptr;
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Mic PCM dump: closed (%llu data bytes)",
                (unsigned long long)dataSize);
}
