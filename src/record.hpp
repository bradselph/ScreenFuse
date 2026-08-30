#pragma once

#include "common.hpp"
#include "image.hpp"
#include "audio.hpp"

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#pragma comment(lib, "mfuuid.lib")

namespace screenfuse {

struct MfApi {
    HRESULT(WINAPI* Startup)(ULONG, DWORD) = nullptr;
    HRESULT(WINAPI* Shutdown)() = nullptr;
    HRESULT(WINAPI* CreateMediaType)(IMFMediaType**) = nullptr;
    HRESULT(WINAPI* CreateSample)(IMFSample**) = nullptr;
    HRESULT(WINAPI* CreateMemoryBuffer)(DWORD, IMFMediaBuffer**) = nullptr;
    HRESULT(WINAPI* CreateSinkWriterFromURL)(LPCWSTR, IMFByteStream*, IMFAttributes*, IMFSinkWriter**) = nullptr;
    bool ok = false;
};

inline const MfApi& Mf() {
    static const MfApi api = [] {
        MfApi resolved;

        const HMODULE plat = LoadLibraryW(L"mfplat.dll");
        const HMODULE readWrite = LoadLibraryW(L"mfreadwrite.dll");
        if (!plat || !readWrite) return resolved;

        resolved.Startup = reinterpret_cast<HRESULT(WINAPI*)(ULONG, DWORD)>(
            GetProcAddress(plat, "MFStartup"));
        resolved.Shutdown = reinterpret_cast<HRESULT(WINAPI*)()>(
            GetProcAddress(plat, "MFShutdown"));
        resolved.CreateMediaType = reinterpret_cast<HRESULT(WINAPI*)(IMFMediaType**)>(
            GetProcAddress(plat, "MFCreateMediaType"));
        resolved.CreateSample = reinterpret_cast<HRESULT(WINAPI*)(IMFSample**)>(
            GetProcAddress(plat, "MFCreateSample"));
        resolved.CreateMemoryBuffer = reinterpret_cast<HRESULT(WINAPI*)(DWORD, IMFMediaBuffer**)>(
            GetProcAddress(plat, "MFCreateMemoryBuffer"));
        resolved.CreateSinkWriterFromURL = reinterpret_cast<HRESULT(WINAPI*)(LPCWSTR, IMFByteStream*, IMFAttributes*, IMFSinkWriter**)>(
            GetProcAddress(readWrite, "MFCreateSinkWriterFromURL"));

        resolved.ok = resolved.Startup && resolved.Shutdown && resolved.CreateMediaType &&
            resolved.CreateSample && resolved.CreateMemoryBuffer && resolved.CreateSinkWriterFromURL;
        return resolved;
    }();
    return api;
}

inline std::atomic<int> g_openRecordings{ 0 };

inline int64_t DefaultRecordBitrate(int width, int height, int fps) {
    int64_t bits = static_cast<int64_t>(width) * height * fps / 10;
    if (bits < 2000000) bits = 2000000;
    if (bits > 50000000) bits = 50000000;
    return bits;
}


class StreamRecorder {
public:
    StreamRecorder(std::wstring outDir, int fps, int64_t videoBitsPerSecond, bool audioExpected, bool startArmed)
        : outDir_(std::move(outDir)),
        fps_(fps < 1 ? 1 : fps),
        videoBitsPerSecond_(videoBitsPerSecond),
        audioExpected_(audioExpected),
        audioQueue_(4 * 1024 * 1024),
        desired_(startArmed) {
        worker_ = std::thread([this] { WorkerBody(); });
    }

    ~StreamRecorder() { Shutdown(); }

    StreamRecorder(const StreamRecorder&) = delete;
    StreamRecorder& operator=(const StreamRecorder&) = delete;

    bool armed() const { return desired_.load(); }

    void Toggle() {
        const bool now = !desired_.load();
        desired_ = now;
        if (now) LogInfo("recording armed - a new file starts at the next merged frame");
        else LogInfo("stopping the recording");
        cv_.notify_all();
    }

    void NoteAudioDead() {
        audioDead_ = true;
        cv_.notify_all();
    }

    void PushVideo(const Image& frame, int64_t stampUs) {
        if (!desired_.load() || !frame.valid()) return;

        std::unique_ptr<Image> buffer;
        {
            std::lock_guard<std::mutex> lock(mutex_);

            if (!freeBuffers_.empty()) {
                buffer = std::move(freeBuffers_.back());
                freeBuffers_.pop_back();
            } else if (allocatedBuffers_ < kPoolSize) {
                buffer = std::make_unique<Image>();
                allocatedBuffers_++;
            }
        }

        if (!buffer) {
            droppedFrames_++;
            const int64_t nowUs = NowUs();
            if (nowUs - lastDropWarnUs_ > 5000000) {
                lastDropWarnUs_ = nowUs;
                LogWarn("recorder is behind the stream - dropped %llu frame(s)",
                    static_cast<unsigned long long>(droppedFrames_.exchange(0)));
            }
            return;
        }

        *buffer = frame;

        if (stampUs <= lastPushStampUs_) stampUs = lastPushStampUs_ + 1000;
        lastPushStampUs_ = stampUs;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            videoQueue_.push_back(VideoEntry{ std::move(buffer), stampUs });
        }
        cv_.notify_all();
    }

    void PushAudio(const uint8_t* data, size_t bytes, int64_t stampUs) {
        if (!desired_.load() || audioDead_.load() || bytes == 0) return;

        AudioPacket packet;
        packet.data.assign(data, data + bytes);
        packet.captureUs = stampUs;
        audioQueue_.Push(std::move(packet));
        cv_.notify_all();
    }

    void Shutdown() {
        if (!worker_.joinable()) return;
        stop_ = true;
        cv_.notify_all();
        worker_.join();
    }

private:
    static constexpr size_t kPoolSize = 4;
    static constexpr int64_t kAudioResyncUs = 60000;
    static constexpr int64_t kMaxVideoHoldUs = 1000000;

    struct VideoEntry {
        std::unique_ptr<Image> image;
        int64_t stampUs = 0;
    };

    void WorkerBody() {
        ComScope com;

        const bool mfStarted = Mf().ok && SUCCEEDED(Mf().Startup(MF_VERSION, MFSTARTUP_FULL));

        for (;;) {
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait_for(lock, std::chrono::milliseconds(100), [this] {
                    return stop_.load() || !videoQueue_.empty();
                });
            }

            if (stop_.load()) break;

            if (!mfStarted) {
                if (desired_.load()) {
                    desired_ = false;
                    LogErr("recording is unavailable: Media Foundation is missing or failed "
                        "to start (on Windows N, install the Media Feature Pack)");
                }
                DiscardQueues();
                continue;
            }

            if (!desired_.load() && writer_) CloseWriter(true);

            ProcessVideoQueue();
            ProcessAudioQueue();
            MaintainTimelines();
        }

        if (mfStarted) {
            ProcessVideoQueue();
            ProcessAudioQueue();
            if (writer_) CloseWriter(true);
            Mf().Shutdown();
        }
    }

    void DiscardQueues() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (VideoEntry& entry : videoQueue_) freeBuffers_.push_back(std::move(entry.image));
            videoQueue_.clear();
        }
        AudioPacket packet;
        while (audioQueue_.Pop(packet)) {}
    }

    void ProcessVideoQueue() {
        for (;;) {
            VideoEntry entry;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (videoQueue_.empty()) return;
                entry = std::move(videoQueue_.front());
                videoQueue_.pop_front();
            }

            if (!desired_.load()) {
                ReturnBuffer(std::move(entry.image));
                continue;
            }

            const int evenWidth = entry.image->width & ~1;
            const int evenHeight = entry.image->height & ~1;

            if (evenWidth < 2 || evenHeight < 2) {
                ReturnBuffer(std::move(entry.image));
                continue;
            }

            if (writer_ && (evenWidth != recordWidth_ || evenHeight != recordHeight_)) {
                LogInfo("the merged frame changed size (%dx%d -> %dx%d) - rolling to a new file",
                    recordWidth_, recordHeight_, evenWidth, evenHeight);
                CloseWriter(true);
            }

            if (!writer_) {
                if (!OpenWriter(evenWidth, evenHeight, entry.stampUs)) {
                    desired_ = false;
                    ReturnBuffer(std::move(entry.image));
                    continue;
                }
            }

            if (held_.image) {
                int64_t durationUs = entry.stampUs - held_.stampUs;
                if (durationUs < 1000) durationUs = 1000;
                WriteHeld(durationUs);
            }

            held_ = std::move(entry);
        }
    }

    void ProcessAudioQueue() {
        AudioPacket packet;
        while (audioQueue_.Pop(packet)) {
            if (!writer_ || audioStream_ == kNoStream || failed_) continue;

            int64_t relStartUs = packet.captureUs - epochUs_;
            size_t frames = packet.data.size() / kAudioBlockAlign;
            const uint8_t* data = packet.data.data();
            if (frames == 0) continue;

            const int64_t durationUs = AudioFramesToUs(static_cast<int64_t>(frames));
            if (relStartUs + durationUs <= 0) continue;

            if (relStartUs < 0) {
                const size_t skip = static_cast<size_t>((-relStartUs) * kAudioSampleRate / 1000000);
                if (skip >= frames) continue;
                data += skip * kAudioBlockAlign;
                frames -= skip;
                relStartUs = 0;
            }

            const int64_t audioPosUs = AudioFramesToUs(audioFramesWritten_);
            const int64_t gapUs = relStartUs - audioPosUs;

            if (gapUs > kAudioResyncUs) {
                WriteSilence(gapUs);
            } else if (gapUs < -kAudioResyncUs) {
                const size_t skip = static_cast<size_t>((-gapUs) * kAudioSampleRate / 1000000);
                if (skip >= frames) continue;
                data += skip * kAudioBlockAlign;
                frames -= skip;
            }

            WriteAudio(data, frames);
        }
    }

    void MaintainTimelines() {
        if (!writer_ || failed_) return;

        if (audioStream_ != kNoStream) {
            const int64_t audioPosUs = AudioFramesToUs(audioFramesWritten_);
            const int64_t videoFrontUs = held_.image ? held_.stampUs - epochUs_ : lastVideoEndUs_;

            if (audioPosUs < videoFrontUs - 500000) {
                WriteSilence(videoFrontUs - 100000 - audioPosUs);
            }

            if (held_.image && audioPosUs > (held_.stampUs - epochUs_) + kMaxVideoHoldUs) {
                const int64_t stamp = held_.stampUs;
                WriteVideoFrame(*held_.image, stamp - epochUs_, kMaxVideoHoldUs);
                held_.stampUs = stamp + kMaxVideoHoldUs;
            }
        }
    }

    bool OpenWriter(int width, int height, int64_t epochUs) {
        if (!EnsureDirectory(outDir_)) {
            LogErr("cannot create %s - recording stopped", ToUtf8(outDir_).c_str());
            return false;
        }

        const std::wstring path = outDir_ + L"\\" + TimestampForFilename() + L"_stream.mp4";

        IMFSinkWriter* writer = nullptr;
        HRESULT hr = Mf().CreateSinkWriterFromURL(path.c_str(), nullptr, nullptr, &writer);
        if (FAILED(hr) || !writer) {
            LogErr("cannot create %s (hr=0x%08lx) - recording stopped",
                ToUtf8(path).c_str(), static_cast<unsigned long>(hr));
            return false;
        }

        const int64_t bitrate = videoBitsPerSecond_ > 0
            ? videoBitsPerSecond_
            : DefaultRecordBitrate(width, height, fps_);

        DWORD videoStream = 0;
        IMFMediaType* type = nullptr;

        bool ok = SUCCEEDED(Mf().CreateMediaType(&type));
        if (ok) ok = SUCCEEDED(type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
        if (ok) ok = SUCCEEDED(type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264));
        if (ok) ok = SUCCEEDED(type->SetUINT32(MF_MT_AVG_BITRATE, static_cast<UINT32>(bitrate)));
        if (ok) ok = SUCCEEDED(MFSetAttributeSize(type, MF_MT_FRAME_SIZE,
            static_cast<UINT32>(width), static_cast<UINT32>(height)));
        if (ok) ok = SUCCEEDED(MFSetAttributeRatio(type, MF_MT_FRAME_RATE, static_cast<UINT32>(fps_), 1));
        if (ok) ok = SUCCEEDED(MFSetAttributeRatio(type, MF_MT_PIXEL_ASPECT_RATIO, 1, 1));
        if (ok) ok = SUCCEEDED(type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive));
        if (ok) ok = SUCCEEDED(writer->AddStream(type, &videoStream));
        if (type) { type->Release(); type = nullptr; }

        if (ok) ok = SUCCEEDED(Mf().CreateMediaType(&type));
        if (ok) ok = SUCCEEDED(type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
        if (ok) ok = SUCCEEDED(type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32));
        if (ok) ok = SUCCEEDED(MFSetAttributeSize(type, MF_MT_FRAME_SIZE,
            static_cast<UINT32>(width), static_cast<UINT32>(height)));
        if (ok) ok = SUCCEEDED(MFSetAttributeRatio(type, MF_MT_FRAME_RATE, static_cast<UINT32>(fps_), 1));
        if (ok) ok = SUCCEEDED(MFSetAttributeRatio(type, MF_MT_PIXEL_ASPECT_RATIO, 1, 1));
        if (ok) ok = SUCCEEDED(type->SetUINT32(MF_MT_DEFAULT_STRIDE,
            static_cast<UINT32>(-4 * width)));
        if (ok) ok = SUCCEEDED(writer->SetInputMediaType(videoStream, type, nullptr));
        if (type) { type->Release(); type = nullptr; }

        DWORD audioStream = kNoStream;
        const bool wantAudio = audioExpected_ && !audioDead_.load();

        if (ok && wantAudio) {
            ok = SUCCEEDED(Mf().CreateMediaType(&type));
            if (ok) ok = SUCCEEDED(type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio));
            if (ok) ok = SUCCEEDED(type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC));
            if (ok) ok = SUCCEEDED(type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, kAudioSampleRate));
            if (ok) ok = SUCCEEDED(type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, kAudioChannels));
            if (ok) ok = SUCCEEDED(type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, kAudioBitsPerSample));
            if (ok) ok = SUCCEEDED(type->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 24000));
            if (ok) ok = SUCCEEDED(type->SetUINT32(MF_MT_AAC_PAYLOAD_TYPE, 0));
            if (ok) ok = SUCCEEDED(writer->AddStream(type, &audioStream));
            if (type) { type->Release(); type = nullptr; }

            if (ok) ok = SUCCEEDED(Mf().CreateMediaType(&type));
            if (ok) ok = SUCCEEDED(type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio));
            if (ok) ok = SUCCEEDED(type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM));
            if (ok) ok = SUCCEEDED(type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, kAudioSampleRate));
            if (ok) ok = SUCCEEDED(type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, kAudioChannels));
            if (ok) ok = SUCCEEDED(type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, kAudioBitsPerSample));
            if (ok) ok = SUCCEEDED(type->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, kAudioBlockAlign));
            if (ok) ok = SUCCEEDED(type->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, kAudioBytesPerSecond));
            if (ok) ok = SUCCEEDED(type->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE));
            if (ok) ok = SUCCEEDED(writer->SetInputMediaType(audioStream, type, nullptr));
            if (type) { type->Release(); type = nullptr; }
        }

        if (ok) {
            const HRESULT begin = writer->BeginWriting();
            ok = SUCCEEDED(begin);
            hr = begin;
        }

        if (!ok) {
            writer->Release();
            if (hr == MF_E_TOPO_CODEC_NOT_FOUND || hr == MF_E_INVALIDMEDIATYPE) {
                LogErr("cannot set up the H.264/AAC encoder (hr=0x%08lx) - on Windows N install "
                    "the Media Feature Pack; note the software encoder tops out at 4096x2304 - "
                    "recording stopped, press r to retry", static_cast<unsigned long>(hr));
            } else {
                LogErr("cannot start the recording (hr=0x%08lx) - press r to retry",
                    static_cast<unsigned long>(hr));
            }
            return false;
        }

        writer_ = writer;
        videoStream_ = videoStream;
        audioStream_ = audioStream;
        path_ = path;
        recordWidth_ = width;
        recordHeight_ = height;
        epochUs_ = epochUs;
        audioFramesWritten_ = 0;
        lastVideoEndUs_ = 0;
        failed_ = false;
        g_openRecordings++;

        LogInfo("recording to %s (%dx%d, %.1f Mbit/s H.264%s)",
            ToUtf8(path).c_str(), width, height,
            static_cast<double>(bitrate) / 1000000.0,
            audioStream_ != kNoStream ? ", 192 kbit/s AAC" : ", no audio");
        return true;
    }

    void CloseWriter(bool logResult) {
        if (!writer_) return;

        if (held_.image && !failed_) {
            WriteHeld(1000000 / fps_);
        } else if (held_.image) {
            ReturnBuffer(std::move(held_.image));
            held_ = VideoEntry{};
        }

        if (audioStream_ != kNoStream && !failed_) {
            const int64_t audioPosUs = AudioFramesToUs(audioFramesWritten_);
            if (audioPosUs < lastVideoEndUs_) WriteSilence(lastVideoEndUs_ - audioPosUs);
        }

        const HRESULT hr = writer_->Finalize();
        writer_->Release();
        writer_ = nullptr;
        g_openRecordings--;

        if (logResult) {
            if (FAILED(hr) || failed_) {
                LogErr("recording %s did not finish cleanly - the file may be unplayable",
                    ToUtf8(path_).c_str());
            } else {
                LogInfo("saved %s (%.1f s)", ToUtf8(path_).c_str(),
                    static_cast<double>(lastVideoEndUs_) / 1000000.0);
            }
        }
    }

    void WriteHeld(int64_t durationUs) {
        WriteVideoFrame(*held_.image, held_.stampUs - epochUs_, durationUs);
        ReturnBuffer(std::move(held_.image));
        held_ = VideoEntry{};
    }

    void WriteVideoFrame(const Image& frame, int64_t stampRelUs, int64_t durationUs) {
        if (failed_ || !writer_) return;

        const size_t rowBytes = static_cast<size_t>(recordWidth_) * 4;
        const DWORD totalBytes = static_cast<DWORD>(rowBytes * static_cast<size_t>(recordHeight_));

        IMFMediaBuffer* buffer = nullptr;
        if (FAILED(Mf().CreateMemoryBuffer(totalBytes, &buffer))) {
            Fail("cannot allocate a video sample buffer");
            return;
        }

        BYTE* dest = nullptr;
        if (FAILED(buffer->Lock(&dest, nullptr, nullptr))) {
            buffer->Release();
            Fail("cannot lock a video sample buffer");
            return;
        }

        for (int y = 0; y < recordHeight_; ++y) {
            const uint8_t* source = frame.pixels.data() +
                static_cast<size_t>(recordHeight_ - 1 - y) * frame.stride();
            memcpy(dest + static_cast<size_t>(y) * rowBytes, source, rowBytes);
        }

        buffer->Unlock();
        buffer->SetCurrentLength(totalBytes);

        WriteSample(videoStream_, buffer, stampRelUs, durationUs);
        buffer->Release();

        if (stampRelUs + durationUs > lastVideoEndUs_) lastVideoEndUs_ = stampRelUs + durationUs;
    }

    void WriteAudio(const uint8_t* data, size_t frames) {
        while (frames > 0 && !failed_) {
            const size_t chunkFrames = frames > kAudioSampleRate ? kAudioSampleRate : frames;
            const DWORD chunkBytes = static_cast<DWORD>(chunkFrames * kAudioBlockAlign);

            IMFMediaBuffer* buffer = nullptr;
            if (FAILED(Mf().CreateMemoryBuffer(chunkBytes, &buffer))) {
                Fail("cannot allocate an audio sample buffer");
                return;
            }

            BYTE* dest = nullptr;
            if (FAILED(buffer->Lock(&dest, nullptr, nullptr))) {
                buffer->Release();
                Fail("cannot lock an audio sample buffer");
                return;
            }

            if (data) memcpy(dest, data, chunkBytes);
            else memset(dest, 0, chunkBytes);

            buffer->Unlock();
            buffer->SetCurrentLength(chunkBytes);

            const int64_t stampRelUs = AudioFramesToUs(audioFramesWritten_);
            const int64_t durationUs = AudioFramesToUs(static_cast<int64_t>(chunkFrames));
            WriteSample(audioStream_, buffer, stampRelUs, durationUs);
            buffer->Release();

            audioFramesWritten_ += static_cast<int64_t>(chunkFrames);
            if (data) data += chunkBytes;
            frames -= chunkFrames;
        }
    }

    void WriteSilence(int64_t durationUs) {
        if (durationUs <= 0) return;
        const int64_t frames = durationUs * kAudioSampleRate / 1000000;
        if (frames > 0) WriteAudio(nullptr, static_cast<size_t>(frames));
    }

    void WriteSample(DWORD stream, IMFMediaBuffer* buffer, int64_t stampRelUs, int64_t durationUs) {
        IMFSample* sample = nullptr;
        if (FAILED(Mf().CreateSample(&sample))) {
            Fail("cannot create a sample");
            return;
        }

        bool ok = SUCCEEDED(sample->AddBuffer(buffer));
        if (ok) ok = SUCCEEDED(sample->SetSampleTime(stampRelUs * 10));
        if (ok) ok = SUCCEEDED(sample->SetSampleDuration(durationUs * 10));
        if (ok) ok = SUCCEEDED(writer_->WriteSample(stream, sample));

        sample->Release();
        if (!ok) Fail("the encoder rejected a sample");
    }

    void Fail(const char* what) {
        if (failed_) return;
        failed_ = true;
        desired_ = false;
        LogErr("recording failed: %s - press r to start a new file", what);
    }

    void ReturnBuffer(std::unique_ptr<Image> buffer) {
        if (!buffer) return;
        std::lock_guard<std::mutex> lock(mutex_);
        freeBuffers_.push_back(std::move(buffer));
    }

    static constexpr DWORD kNoStream = 0xFFFFFFFF;

    std::wstring outDir_;
    int fps_ = 30;
    int64_t videoBitsPerSecond_ = 0;
    bool audioExpected_ = false;

    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<VideoEntry> videoQueue_;
    std::vector<std::unique_ptr<Image>> freeBuffers_;
    size_t allocatedBuffers_ = 0;

    AudioPacketQueue audioQueue_;

    std::atomic<bool> desired_{ false };
    std::atomic<bool> stop_{ false };
    std::atomic<bool> audioDead_{ false };
    std::atomic<uint64_t> droppedFrames_{ 0 };
    int64_t lastDropWarnUs_ = 0;
    int64_t lastPushStampUs_ = 0;

    IMFSinkWriter* writer_ = nullptr;
    DWORD videoStream_ = 0;
    DWORD audioStream_ = kNoStream;
    std::wstring path_;
    int recordWidth_ = 0;
    int recordHeight_ = 0;
    int64_t epochUs_ = 0;
    int64_t audioFramesWritten_ = 0;
    int64_t lastVideoEndUs_ = 0;
    VideoEntry held_;
    bool failed_ = false;

    std::thread worker_;
};


} // namespace screenfuse
