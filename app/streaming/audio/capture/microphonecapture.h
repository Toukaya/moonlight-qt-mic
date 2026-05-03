#pragma once

#include <QObject>
#include <atomic>
#include <array>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <SDL.h>
#include <opus.h>

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
    void encoderLoop();

    SDL_AudioDeviceID m_DeviceId;
    SDL_AudioSpec m_ObtainedSpec;
    OpusEncoder* m_Encoder;
    std::vector<opus_int16> m_SampleBuffer;
    std::array<unsigned char, 1400> m_EncodedPacket;
    std::atomic_bool m_Streaming;
    std::atomic_bool m_StopEncoderThread;
    bool m_Initialized;
    bool m_Enabled;
    bool m_FirstPacketLogged;
    std::mutex m_BufferMutex;
    std::condition_variable m_BufferCondition;
    std::thread m_EncoderThread;

    // Negotiated at runtime from the device's native config. Opus accepts
    // only 8/12/16/24/48 kHz mono or stereo, so when the device's native
    // rate is one of those, we encode at it directly (no resample). For
    // any other native rate (e.g. 44.1 kHz on most macOS built-in mics)
    // we fall back to 48 kHz and let SDL resample.
    int m_OpusRate;
    int m_OpusChannels;
    int m_FrameSize;     // samples per channel for one 20 ms Opus frame
};
