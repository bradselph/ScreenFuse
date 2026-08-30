#pragma once

#include "common.hpp"

#include <mmdeviceapi.h>
#include <audioclient.h>

#include <algorithm>
#include <atomic>
#include <deque>
#include <mutex>

namespace screenfuse {

constexpr uint32_t kAudioSampleRate = 48000;
constexpr uint16_t kAudioChannels = 2;
constexpr uint16_t kAudioBitsPerSample = 16;
constexpr uint32_t kAudioBlockAlign = kAudioChannels * (kAudioBitsPerSample / 8);
constexpr uint32_t kAudioBytesPerSecond = kAudioSampleRate * kAudioBlockAlign;

inline WAVEFORMATEX AudioWireFormat() {
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = kAudioChannels;
    format.nSamplesPerSec = kAudioSampleRate;
    format.nAvgBytesPerSec = kAudioBytesPerSecond;
    format.nBlockAlign = static_cast<WORD>(kAudioBlockAlign);
    format.wBitsPerSample = kAudioBitsPerSample;
    return format;
}

inline int64_t AudioFramesToUs(int64_t frames) {
    return frames * 1000000 / kAudioSampleRate;
}

inline std::wstring DefaultRenderDeviceId(IMMDeviceEnumerator* enumerator) {
    if (!enumerator) return {};

    IMMDevice* device = nullptr;
    if (FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device)) || !device) return {};

    std::wstring id;
    wchar_t* raw = nullptr;
    if (SUCCEEDED(device->GetId(&raw)) && raw) {
        id = raw;
        CoTaskMemFree(raw);
    }

    device->Release();
    return id;
}


class LoopbackCapture {
public:
    ~LoopbackCapture() { Close(); }

    bool Open(std::string& error) {
        Close();

        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&enumerator_)))) {
            error = "cannot create the audio device enumerator";
            return false;
        }

        HRESULT hr = enumerator_->GetDefaultAudioEndpoint(eRender, eConsole, &device_);
        if (FAILED(hr) || !device_) {
            error = "no default audio output device on this machine";
            Close();
            return false;
        }

        wchar_t* rawId = nullptr;
        if (SUCCEEDED(device_->GetId(&rawId)) && rawId) {
            deviceId_ = rawId;
            CoTaskMemFree(rawId);
        }

        if (FAILED(device_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
            reinterpret_cast<void**>(&client_)))) {
            error = "cannot activate an audio client on the output device";
            Close();
            return false;
        }

        WAVEFORMATEX* mix = nullptr;
        if (SUCCEEDED(client_->GetMixFormat(&mix)) && mix) {
            mixRate_ = mix->nSamplesPerSec;
            mixChannels_ = mix->nChannels;
            CoTaskMemFree(mix);
        }

        const WAVEFORMATEX want = AudioWireFormat();
        hr = client_->Initialize(AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
            5000000, 0, &want, nullptr);

        if (FAILED(hr)) {
            error = "cannot open a loopback capture of the output device (hr=0x" +
                ToHex(reinterpret_cast<const uint8_t*>(&hr), sizeof(hr)) + ")";
            Close();
            return false;
        }

        if (FAILED(client_->GetService(IID_PPV_ARGS(&capture_)))) {
            error = "cannot get the capture service from the audio client";
            Close();
            return false;
        }

        OpenKeepAlive();

        if (FAILED(client_->Start())) {
            error = "cannot start the loopback capture";
            Close();
            return false;
        }

        return true;
    }

    void Close() {
        if (client_) client_->Stop();
        if (keepAliveClient_) keepAliveClient_->Stop();

        if (capture_) { capture_->Release(); capture_ = nullptr; }
        if (client_) { client_->Release(); client_ = nullptr; }
        if (keepAliveRender_) { keepAliveRender_->Release(); keepAliveRender_ = nullptr; }
        if (keepAliveClient_) { keepAliveClient_->Release(); keepAliveClient_ = nullptr; }
        if (device_) { device_->Release(); device_ = nullptr; }
        if (enumerator_) { enumerator_->Release(); enumerator_ = nullptr; }

        deviceId_.clear();
        keepAliveFrames_ = 0;
        mixRate_ = 0;
        mixChannels_ = 0;
    }

    bool ok() const { return capture_ != nullptr; }
    uint32_t mixRate() const { return mixRate_; }
    uint16_t mixChannels() const { return mixChannels_; }

    bool DefaultDeviceChanged() {
        if (!enumerator_) return false;
        const std::wstring current = DefaultRenderDeviceId(enumerator_);
        return !current.empty() && current != deviceId_;
    }

    bool Read(std::vector<uint8_t>& out, int64_t& firstStampUs, std::string& error) {
        if (!capture_) {
            error = "capture is not open";
            return false;
        }

        TopUpKeepAlive();

        for (;;) {
            UINT32 packetFrames = 0;
            HRESULT hr = capture_->GetNextPacketSize(&packetFrames);
            if (FAILED(hr)) {
                error = "the audio device went away (GetNextPacketSize)";
                return false;
            }
            if (packetFrames == 0) return true;

            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            UINT64 qpcPosition = 0;

            hr = capture_->GetBuffer(&data, &frames, &flags, nullptr, &qpcPosition);
            if (hr == AUDCLNT_S_BUFFER_EMPTY) return true;
            if (FAILED(hr)) {
                error = "the audio device went away (GetBuffer)";
                return false;
            }

            if (frames > 0) {
                int64_t stampUs = static_cast<int64_t>(qpcPosition / 10);
                const int64_t nowUs = NowUs();
                if ((flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR) != 0 ||
                    stampUs > nowUs + 1000000 || stampUs < nowUs - 2000000) {
                    stampUs = nowUs - AudioFramesToUs(frames);
                }

                if (out.empty()) firstStampUs = stampUs;

                const size_t bytes = static_cast<size_t>(frames) * kAudioBlockAlign;
                const size_t offset = out.size();
                out.resize(offset + bytes);

                if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0 || !data) {
                    memset(out.data() + offset, 0, bytes);
                } else {
                    memcpy(out.data() + offset, data, bytes);
                }
            }

            capture_->ReleaseBuffer(frames);
        }
    }

private:
    void OpenKeepAlive() {
        if (FAILED(device_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
            reinterpret_cast<void**>(&keepAliveClient_)))) {
            return;
        }

        const WAVEFORMATEX want = AudioWireFormat();
        if (FAILED(keepAliveClient_->Initialize(AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
            2000000, 0, &want, nullptr)) ||
            FAILED(keepAliveClient_->GetService(IID_PPV_ARGS(&keepAliveRender_))) ||
            FAILED(keepAliveClient_->GetBufferSize(&keepAliveFrames_))) {
            if (keepAliveRender_) { keepAliveRender_->Release(); keepAliveRender_ = nullptr; }
            keepAliveClient_->Release();
            keepAliveClient_ = nullptr;
            return;
        }

        TopUpKeepAlive();
        keepAliveClient_->Start();
    }

    void TopUpKeepAlive() {
        if (!keepAliveRender_ || keepAliveFrames_ == 0) return;

        UINT32 padding = 0;
        if (FAILED(keepAliveClient_->GetCurrentPadding(&padding))) return;
        if (padding >= keepAliveFrames_ / 2) return;

        const UINT32 want = keepAliveFrames_ - padding;
        BYTE* buffer = nullptr;
        if (SUCCEEDED(keepAliveRender_->GetBuffer(want, &buffer))) {
            keepAliveRender_->ReleaseBuffer(want, AUDCLNT_BUFFERFLAGS_SILENT);
        }
    }

    IMMDeviceEnumerator* enumerator_ = nullptr;
    IMMDevice* device_ = nullptr;
    IAudioClient* client_ = nullptr;
    IAudioCaptureClient* capture_ = nullptr;
    IAudioClient* keepAliveClient_ = nullptr;
    IAudioRenderClient* keepAliveRender_ = nullptr;
    UINT32 keepAliveFrames_ = 0;
    std::wstring deviceId_;
    uint32_t mixRate_ = 0;
    uint16_t mixChannels_ = 0;
};


class AudioRenderer {
public:
    ~AudioRenderer() { Close(); }

    bool Open(int targetBufferMs, std::string& error) {
        Close();

        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&enumerator_)))) {
            error = "cannot create the audio device enumerator";
            return false;
        }

        IMMDevice* device = nullptr;
        HRESULT hr = enumerator_->GetDefaultAudioEndpoint(eRender, eConsole, &device);
        if (FAILED(hr) || !device) {
            error = "no default audio output device on this PC";
            Close();
            return false;
        }

        wchar_t* rawId = nullptr;
        if (SUCCEEDED(device->GetId(&rawId)) && rawId) {
            deviceId_ = rawId;
            CoTaskMemFree(rawId);
        }

        hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
            reinterpret_cast<void**>(&client_));
        device->Release();

        if (FAILED(hr)) {
            error = "cannot activate an audio client on the output device";
            Close();
            return false;
        }

        const WAVEFORMATEX want = AudioWireFormat();
        const REFERENCE_TIME bufferHns = static_cast<REFERENCE_TIME>(
            (targetBufferMs < 100 ? 400 : targetBufferMs * 4)) * 10000;

        hr = client_->Initialize(AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
            bufferHns, 0, &want, nullptr);

        if (FAILED(hr)) {
            error = "cannot open the output device for playback (hr=0x" +
                ToHex(reinterpret_cast<const uint8_t*>(&hr), sizeof(hr)) + ")";
            Close();
            return false;
        }

        if (FAILED(client_->GetBufferSize(&bufferFrames_)) ||
            FAILED(client_->GetService(IID_PPV_ARGS(&render_)))) {
            error = "cannot get the render service from the audio client";
            Close();
            return false;
        }

        const UINT32 preload = static_cast<UINT32>(
            std::min<int64_t>(bufferFrames_,
                static_cast<int64_t>(targetBufferMs) * kAudioSampleRate / 1000));
        BYTE* buffer = nullptr;
        if (preload > 0 && SUCCEEDED(render_->GetBuffer(preload, &buffer))) {
            render_->ReleaseBuffer(preload, AUDCLNT_BUFFERFLAGS_SILENT);
        }

        if (FAILED(client_->Start())) {
            error = "cannot start playback";
            Close();
            return false;
        }

        return true;
    }

    void Close() {
        if (client_) client_->Stop();
        if (render_) { render_->Release(); render_ = nullptr; }
        if (client_) { client_->Release(); client_ = nullptr; }
        if (enumerator_) { enumerator_->Release(); enumerator_ = nullptr; }
        deviceId_.clear();
        bufferFrames_ = 0;
        dead_ = false;
    }

    bool ok() const { return render_ != nullptr && !dead_; }

    bool DefaultDeviceChanged() {
        if (!enumerator_) return false;
        const std::wstring current = DefaultRenderDeviceId(enumerator_);
        return !current.empty() && current != deviceId_;
    }

    int paddingMs() {
        if (!render_) return 0;
        UINT32 padding = 0;
        if (FAILED(client_->GetCurrentPadding(&padding))) {
            dead_ = true;
            return 0;
        }
        return static_cast<int>(AudioFramesToUs(padding) / 1000);
    }

    size_t Write(const uint8_t* data, size_t bytes) {
        if (!render_ || dead_ || bytes < kAudioBlockAlign) return 0;

        UINT32 padding = 0;
        if (FAILED(client_->GetCurrentPadding(&padding))) {
            dead_ = true;
            return 0;
        }

        const UINT32 room = bufferFrames_ > padding ? bufferFrames_ - padding : 0;
        const UINT32 have = static_cast<UINT32>(bytes / kAudioBlockAlign);
        const UINT32 frames = room < have ? room : have;
        if (frames == 0) return 0;

        BYTE* buffer = nullptr;
        if (FAILED(render_->GetBuffer(frames, &buffer))) {
            dead_ = true;
            return 0;
        }

        memcpy(buffer, data, static_cast<size_t>(frames) * kAudioBlockAlign);
        if (FAILED(render_->ReleaseBuffer(frames, 0))) {
            dead_ = true;
            return 0;
        }

        return static_cast<size_t>(frames) * kAudioBlockAlign;
    }

private:
    IMMDeviceEnumerator* enumerator_ = nullptr;
    IAudioClient* client_ = nullptr;
    IAudioRenderClient* render_ = nullptr;
    UINT32 bufferFrames_ = 0;
    std::wstring deviceId_;
    bool dead_ = false;
};


struct AudioPacket {
    std::vector<uint8_t> data;
    int64_t captureUs = 0;
};

class AudioPacketQueue {
public:
    explicit AudioPacketQueue(size_t maxBytes) : maxBytes_(maxBytes) {}

    void Push(AudioPacket packet) {
        std::lock_guard<std::mutex> lock(mutex_);
        bytes_ += packet.data.size();
        packets_.push_back(std::move(packet));

        while (bytes_ > maxBytes_ && packets_.size() > 1) {
            bytes_ -= packets_.front().data.size();
            packets_.pop_front();
            dropped_++;
        }
    }

    bool Pop(AudioPacket& out) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (packets_.empty()) return false;
        out = std::move(packets_.front());
        packets_.pop_front();
        bytes_ -= out.data.size();
        return true;
    }

    size_t bytes() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return bytes_;
    }

    uint64_t TakeDropped() {
        std::lock_guard<std::mutex> lock(mutex_);
        const uint64_t taken = dropped_;
        dropped_ = 0;
        return taken;
    }

    void Clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        packets_.clear();
        bytes_ = 0;
    }

private:
    mutable std::mutex mutex_;
    std::deque<AudioPacket> packets_;
    size_t bytes_ = 0;
    size_t maxBytes_ = 0;
    uint64_t dropped_ = 0;
};

} // namespace screenfuse
