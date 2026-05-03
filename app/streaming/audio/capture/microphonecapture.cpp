#include "microphonecapture.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <Limelight.h>

MicrophoneCapture::MicrophoneCapture(QObject* parent)
    : QObject(parent)
    , m_DeviceId(0)
    , m_ObtainedSpec({})
    , m_SampleRate(48000)
    , m_Channels(1)
    , m_BytesPerSample(2)
    , m_SampleFormatId(LI_MIC_FMT_S16LE)
    , m_WireFrameSamples(480)
    , m_FrameDurationMs(10)
    , m_Streaming(false)
    , m_StopSenderThread(false)
    , m_Initialized(false)
    , m_Enabled(false)
    , m_FirstPacketLogged(false)
    , m_StatPktsSent(0)
    , m_StatBytesSent(0)
    , m_StatBufHwBytes(0)
    , m_StatTrims(0)
    , m_StatPeakThisSec(0.0f)
{
}

MicrophoneCapture::~MicrophoneCapture()
{
    stop();

    m_StopSenderThread.store(true, std::memory_order_release);
    m_BufferCondition.notify_all();
    if (m_SenderThread.joinable()) {
        m_SenderThread.join();
    }

    if (m_DeviceId != 0) {
        SDL_CloseAudioDevice(m_DeviceId);
        m_DeviceId = 0;
    }
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

    // Check that the host advertised a PCM mic config before opening any device.
    LI_MIC_CONFIG cfg = {};
    if (LiGetNegotiatedMicConfig(&cfg) != 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Host did not advertise PCM mic format; microphone disabled");
        return false;
    }

    // Validate the negotiated format. s24le requires pack-down which is
    // not implemented — refuse it clearly rather than silently misbehaving.
    if (cfg.sampleFormatId == LI_MIC_FMT_S24LE) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Host requested s24le mic format which is not supported by this client; microphone disabled");
        return false;
    }

    // MTU guard: validate that the host-advertised frame fits in one packet.
    int bytesPerSample = (cfg.bitsPerSample + 7) / 8;
    int payloadBytes = cfg.sampleRate * cfg.channels * bytesPerSample * cfg.frameDurationMs / 1000;
    // AES-CBC worst-case pad: 16 bytes. Header: 12 bytes.
    if (payloadBytes + 12 + 16 > 1400) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Host advertised mic frame size (%d bytes payload) exceeds MTU even with smallest frame; microphone disabled",
                    payloadBytes);
        return false;
    }

    // Map sampleFormatId to SDL format.
    SDL_AudioFormat sdlFormat;
    switch (cfg.sampleFormatId) {
    case LI_MIC_FMT_S16LE:
        sdlFormat = AUDIO_S16LSB;
        break;
    case LI_MIC_FMT_S32LE:
        sdlFormat = AUDIO_S32LSB;
        break;
    case LI_MIC_FMT_F32LE:
        sdlFormat = AUDIO_F32LSB;
        break;
    default:
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Unknown mic sampleFormatId %d; microphone disabled", cfg.sampleFormatId);
        return false;
    }

    const char* selectedDevice = deviceName.empty() ? nullptr : deviceName.c_str();

    // Phase 1: probe the device's preferred config.
    SDL_AudioSpec probeDesired = {};
    probeDesired.freq = cfg.sampleRate;
    probeDesired.format = sdlFormat;
    probeDesired.channels = static_cast<Uint8>(cfg.channels);
    probeDesired.samples = static_cast<Uint16>(cfg.sampleRate * cfg.frameDurationMs / 1000);
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

    // Phase 2: open the device at the exact format negotiated with the host.
    // We do not allow SDL to change the format or channel count because the
    // wire format is fixed. Only sample count per callback is allowed to vary.
    SDL_AudioSpec desired = {};
    desired.freq = cfg.sampleRate;
    desired.format = sdlFormat;
    desired.channels = static_cast<Uint8>(cfg.channels);
    desired.samples = static_cast<Uint16>(cfg.sampleRate * cfg.frameDurationMs / 1000);
    desired.callback = &MicrophoneCapture::audioCallback;
    desired.userdata = this;

    m_DeviceId = SDL_OpenAudioDevice(selectedDevice, 1, &desired, &m_ObtainedSpec,
                                     SDL_AUDIO_ALLOW_SAMPLES_CHANGE);
    if (m_DeviceId == 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "SDL_OpenAudioDevice() failed for microphone capture: %s",
                    SDL_GetError());
        return false;
    }

    // Validate that SDL gave us exactly the format and channels we asked for.
    if (m_ObtainedSpec.format != sdlFormat ||
            m_ObtainedSpec.freq != cfg.sampleRate ||
            m_ObtainedSpec.channels != static_cast<Uint8>(cfg.channels)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Microphone capture format mismatch: got %d Hz, %d channels, format 0x%x; "
                    "wanted %d Hz, %d channels, format 0x%x",
                    m_ObtainedSpec.freq, m_ObtainedSpec.channels, m_ObtainedSpec.format,
                    cfg.sampleRate, cfg.channels, (unsigned)sdlFormat);
        SDL_CloseAudioDevice(m_DeviceId);
        m_DeviceId = 0;
        return false;
    }

    m_SampleRate     = cfg.sampleRate;
    m_Channels       = cfg.channels;
    m_BytesPerSample = bytesPerSample;
    m_SampleFormatId = cfg.sampleFormatId;
    m_WireFrameSamples = cfg.sampleRate * cfg.frameDurationMs / 1000;
    m_FrameDurationMs  = cfg.frameDurationMs;

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Microphone capture: device='%s'; %d Hz / %d ch / %d-bit (fmt %d) / %d ms frames "
                "(%d samples, %d bytes/frame)",
                selectedDevice ? selectedDevice : "<default>",
                m_SampleRate, m_Channels, cfg.bitsPerSample, m_SampleFormatId,
                m_FrameDurationMs, m_WireFrameSamples,
                m_WireFrameSamples * m_Channels * m_BytesPerSample);

    SDL_PauseAudioDevice(m_DeviceId, 1);
    const int wireFrameBytes = m_WireFrameSamples * m_Channels * m_BytesPerSample;
    m_SampleBuffer.reserve(static_cast<size_t>(wireFrameBytes) * 12);
    m_StopSenderThread.store(false, std::memory_order_release);
    m_SenderThread = std::thread(&MicrophoneCapture::senderLoop, this);
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

    const int wireFrameBytes = m_WireFrameSamples * m_Channels * m_BytesPerSample;
    // ~240 ms ceiling: 12 wire frames max.
    const size_t maxBufferedBytes = static_cast<size_t>(wireFrameBytes) * 12;
    int trimBytes = 0;

    {
        std::lock_guard<std::mutex> lock(m_BufferMutex);
        m_SampleBuffer.insert(m_SampleBuffer.end(), stream, stream + len);

        if (m_SampleBuffer.size() > maxBufferedBytes) {
            const size_t excess = m_SampleBuffer.size() - maxBufferedBytes;
            m_SampleBuffer.erase(m_SampleBuffer.begin(),
                                 m_SampleBuffer.begin() + static_cast<std::ptrdiff_t>(excess));
            // Round trim count to whole frames for stats
            trimBytes = static_cast<int>(excess);
            m_StatTrims += (trimBytes + wireFrameBytes - 1) / wireFrameBytes;
        }

        if (m_SampleBuffer.size() > m_StatBufHwBytes) {
            m_StatBufHwBytes = m_SampleBuffer.size();
        }
    }
    m_BufferCondition.notify_one();
}

float MicrophoneCapture::computePeak(const uint8_t* data, int byteCount) const
{
    float peak = 0.0f;
    const int frameCount = byteCount / m_BytesPerSample;

    if (m_SampleFormatId == LI_MIC_FMT_S16LE) {
        const int16_t* s = reinterpret_cast<const int16_t*>(data);
        for (int i = 0; i < frameCount; ++i) {
            float v = static_cast<float>(s[i]) / 32768.0f;
            float a = std::fabs(v);
            if (a > peak) { peak = a; }
        }
    } else if (m_SampleFormatId == LI_MIC_FMT_S32LE) {
        const int32_t* s = reinterpret_cast<const int32_t*>(data);
        for (int i = 0; i < frameCount; ++i) {
            float v = static_cast<float>(s[i]) / 2147483648.0f;
            float a = std::fabs(v);
            if (a > peak) { peak = a; }
        }
    } else if (m_SampleFormatId == LI_MIC_FMT_F32LE) {
        const float* s = reinterpret_cast<const float*>(data);
        for (int i = 0; i < frameCount; ++i) {
            float a = std::fabs(s[i]);
            if (a > peak) { peak = a; }
        }
    }

    return peak;
}

void MicrophoneCapture::senderLoop()
{
    const int wireFrameBytes = m_WireFrameSamples * m_Channels * m_BytesPerSample;
    std::vector<uint8_t> frame(wireFrameBytes);
    const auto frameDuration = std::chrono::milliseconds(m_FrameDurationMs);
    auto nextSendDeadline = std::chrono::steady_clock::now();
    bool pacingActive = false;

    // Stats accounting
    auto statsWindowStart = std::chrono::steady_clock::now();
    int statPkts = 0;
    int statBytes = 0;
    float statPeak = 0.0f;
    size_t statBufHw = 0;
    int statTrims = 0;

    for (;;) {
        {
            std::unique_lock<std::mutex> lock(m_BufferMutex);
            m_BufferCondition.wait(lock, [this, wireFrameBytes] {
                return m_StopSenderThread.load(std::memory_order_acquire) ||
                       (m_Streaming.load(std::memory_order_acquire) &&
                        static_cast<int>(m_SampleBuffer.size()) >= wireFrameBytes);
            });

            if (m_StopSenderThread.load(std::memory_order_acquire)) {
                break;
            }

            if (!m_Streaming.load(std::memory_order_acquire) ||
                    static_cast<int>(m_SampleBuffer.size()) < wireFrameBytes) {
                pacingActive = false;
                continue;
            }

            std::copy_n(m_SampleBuffer.begin(), wireFrameBytes, frame.begin());
            m_SampleBuffer.erase(m_SampleBuffer.begin(),
                                 m_SampleBuffer.begin() + wireFrameBytes);

            if (m_SampleBuffer.size() > statBufHw) {
                statBufHw = m_SampleBuffer.size();
            }
            statTrims += m_StatTrims;
            m_StatTrims = 0;
        }

        const auto now = std::chrono::steady_clock::now();
        if (!pacingActive) {
            nextSendDeadline = now;
            pacingActive = true;
        } else if (now > nextSendDeadline + (frameDuration * 2)) {
            nextSendDeadline = now;
        }

        if (nextSendDeadline > now) {
            std::this_thread::sleep_until(nextSendDeadline);
        }
        nextSendDeadline += frameDuration;

        float framePeak = computePeak(frame.data(), wireFrameBytes);
        if (framePeak > statPeak) { statPeak = framePeak; }

        int sendResult = LiSendMicrophonePcmData(frame.data(), wireFrameBytes,
                                                  static_cast<uint32_t>(m_WireFrameSamples));
        if (sendResult >= 0) {
            ++statPkts;
            statBytes += wireFrameBytes;

            if (!m_FirstPacketLogged) {
                m_FirstPacketLogged = true;
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                            "Sent first client microphone PCM packet (%d bytes raw)",
                            wireFrameBytes);
            }
        } else {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "LiSendMicrophonePcmData() failed for microphone capture");
        }

        // Per-second stats log
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - statsWindowStart).count();
        if (elapsed >= 1000) {
            const double dbfs = (statPeak > 0.0f)
                ? 20.0 * std::log10(static_cast<double>(statPeak))
                : -std::numeric_limits<double>::infinity();
            const double kbps = (elapsed > 0)
                ? static_cast<double>(statBytes) * 8.0 / static_cast<double>(elapsed)
                : 0.0;
            const size_t bufMs = (m_SampleRate > 0 && m_Channels > 0 && m_BytesPerSample > 0)
                ? statBufHw * 1000 / (static_cast<size_t>(m_SampleRate) * m_Channels * m_BytesPerSample)
                : 0;

            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "Mic stats[1s]: peak=%.1fdBFS, sent=%d pkts (%.0f kbps avg), "
                        "buf-hw=%zu values (%zu ms), trims=%d",
                        dbfs, statPkts, kbps,
                        statBufHw / static_cast<size_t>(m_BytesPerSample),
                        bufMs,
                        statTrims);

            statPkts  = 0;
            statBytes = 0;
            statPeak  = 0.0f;
            statBufHw = 0;
            statTrims = 0;
            statsWindowStart = std::chrono::steady_clock::now();
        }
    }
}

void MicrophoneCapture::clearBufferedSamples()
{
    {
        std::lock_guard<std::mutex> lock(m_BufferMutex);
        m_SampleBuffer.clear();
    }
}
