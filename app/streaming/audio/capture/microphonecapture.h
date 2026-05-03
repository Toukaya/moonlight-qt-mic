#pragma once

#include <QObject>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <SDL.h>

class MicrophoneCapture : public QObject
{
    Q_OBJECT

public:
    explicit MicrophoneCapture(QObject* parent = nullptr);
    ~MicrophoneCapture() override;

    bool initialize(const std::string& deviceName = {});
    bool start();
    void stop();

    void setEnabled(bool enabled);
    bool isEnabled() const;
    bool isStreaming() const;

private:
    static void audioCallback(void* userdata, Uint8* stream, int len);
    void handleAudioData(const Uint8* stream, int len);
    void clearBufferedSamples();
    void senderLoop();

    // Returns the peak absolute level in the range [0.0, 1.0] over
    // the provided interleaved sample buffer. Handles any configured format.
    float computePeak(const uint8_t* data, int byteCount) const;

    SDL_AudioDeviceID m_DeviceId;
    SDL_AudioSpec m_ObtainedSpec;

    // Negotiated from host SDP
    int m_SampleRate;
    int m_Channels;
    int m_BytesPerSample;
    int m_SampleFormatId;
    int m_WireFrameSamples;   // samples-per-channel per sent frame
    int m_FrameDurationMs;

    // Raw byte sample ring buffer (interleaved samples, any bit depth)
    std::vector<uint8_t> m_SampleBuffer;

    std::atomic_bool m_Streaming;
    std::atomic_bool m_StopSenderThread;
    bool m_Initialized;
    bool m_Enabled;
    bool m_FirstPacketLogged;
    std::mutex m_BufferMutex;
    std::condition_variable m_BufferCondition;
    std::thread m_SenderThread;

    // Per-second stats
    int m_StatPktsSent;
    int m_StatBytesSent;
    size_t m_StatBufHwBytes;
    int m_StatTrims;
    float m_StatPeakThisSec;
};
