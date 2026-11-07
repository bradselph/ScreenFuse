#pragma once

#include "common.hpp"
#include "capture.hpp"
#include "image.hpp"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

namespace screenfuse {

inline unsigned PixelWorkers() {
    static const unsigned workers = [] {
        unsigned count = std::thread::hardware_concurrency();
        if (count == 0) count = 2;
        if (count > 8) count = 8;
        return count;
    }();
    return workers;
}

class PixelPool {
public:
    using PartFn = void (*)(void* context, int rowBegin, int rowEnd);

    static PixelPool& Get() {
        static PixelPool* pool = new PixelPool(PixelWorkers() - 1);
        return *pool;
    }

    void Run(int height, int parts, PartFn fn, void* context) {
        auto batch = std::make_shared<Batch>();
        batch->fn = fn;
        batch->context = context;
        batch->height = height;
        batch->parts = parts;
        batch->chunk = (height + parts - 1) / parts;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push_back(batch);
        }
        wake_.notify_all();

        while (RunPart(*batch)) {}

        {
            std::unique_lock<std::mutex> lock(batch->mutex);
            batch->finished.wait(lock, [&] { return batch->done.load() == batch->parts; });
        }

        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = queue_.begin(); it != queue_.end(); ++it) {
            if (*it == batch) {
                queue_.erase(it);
                break;
            }
        }
    }

private:
    struct Batch {
        PartFn fn = nullptr;
        void* context = nullptr;
        int height = 0;
        int parts = 0;
        int chunk = 0;
        std::atomic<int> next{ 0 };
        std::atomic<int> done{ 0 };
        std::mutex mutex;
        std::condition_variable finished;
    };

    explicit PixelPool(unsigned threads) {
        for (unsigned i = 0; i < threads; ++i) std::thread([this] { WorkerLoop(); }).detach();
    }

    static bool RunPart(Batch& batch) {
        const int part = batch.next.fetch_add(1);
        if (part >= batch.parts) return false;

        const int begin = part * batch.chunk;
        const int end = begin + batch.chunk < batch.height ? begin + batch.chunk : batch.height;
        if (begin < end) batch.fn(batch.context, begin, end);

        if (batch.done.fetch_add(1) + 1 == batch.parts) {
            std::lock_guard<std::mutex> lock(batch.mutex);
            batch.finished.notify_all();
        }
        return true;
    }

    void WorkerLoop() {
        for (;;) {
            std::shared_ptr<Batch> batch;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                wake_.wait(lock, [this] { return !queue_.empty(); });
                batch = queue_.front();
                if (batch->next.load() >= batch->parts) {
                    queue_.pop_front();
                    continue;
                }
            }
            RunPart(*batch);
        }
    }

    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<std::shared_ptr<Batch>> queue_;
};

template <typename Body>
inline void ParallelRows(int height, Body body) {
    const unsigned workers = PixelWorkers();

    if (height < 64 || workers <= 1) {
        body(0, height);
        return;
    }

    PixelPool::Get().Run(height, static_cast<int>(workers),
        [](void* context, int rowBegin, int rowEnd) { (*static_cast<Body*>(context))(rowBegin, rowEnd); },
        &body);
}

struct BilinearTap {
    int first = 0;
    int second = 0;
    int weight = 0;
};

inline void BuildBilinearTaps(int sourceSize, int outSize, std::vector<BilinearTap>& taps) {
    taps.resize(static_cast<size_t>(outSize));
    const double ratio = static_cast<double>(sourceSize) / outSize;

    for (int i = 0; i < outSize; ++i) {
        const double s = (i + 0.5) * ratio - 0.5;
        const int first = s > 0.0 ? static_cast<int>(s) : 0;
        const double fraction = s - first > 0.0 ? s - first : 0.0;

        BilinearTap& tap = taps[static_cast<size_t>(i)];
        tap.first = first;
        tap.second = first + 1 < sourceSize ? first + 1 : sourceSize - 1;
        tap.weight = static_cast<int>(fraction * 256.0 + 0.5);
    }
}

inline void BuildAreaSpans(int sourceSize, int outSize, std::vector<int>& starts) {
    starts.resize(static_cast<size_t>(outSize) + 1);
    for (int i = 0; i <= outSize; ++i) {
        starts[static_cast<size_t>(i)] = static_cast<int>(static_cast<int64_t>(i) * sourceSize / outSize);
    }
}

inline void ScaleToInto(const Image& source, Image& out, int width, int height) {
    if (!source.valid() || width <= 0 || height <= 0) return;

    if (out.width != width || out.height != height) out.reshape(width, height);

    const uint8_t* src = source.pixels.data();
    const size_t srcStride = source.stride();

    if (source.width >= width * 2 && source.height >= height * 2) {
        std::vector<int> columns;
        std::vector<int> rows;
        BuildAreaSpans(source.width, width, columns);
        BuildAreaSpans(source.height, height, rows);

        ParallelRows(height, [&](int rowBegin, int rowEnd) {
            for (int y = rowBegin; y < rowEnd; ++y) {
                const int y0 = rows[static_cast<size_t>(y)];
                const int y1 = rows[static_cast<size_t>(y) + 1];
                uint8_t* dst = out.pixels.data() + static_cast<size_t>(y) * out.stride();

                for (int x = 0; x < width; ++x, dst += 4) {
                    const int x0 = columns[static_cast<size_t>(x)];
                    const int x1 = columns[static_cast<size_t>(x) + 1];
                    uint32_t b = 0, g = 0, r = 0;

                    for (int sy = y0; sy < y1; ++sy) {
                        const uint8_t* p = src + static_cast<size_t>(sy) * srcStride + static_cast<size_t>(x0) * 4;
                        for (int sx = x0; sx < x1; ++sx, p += 4) {
                            b += p[0];
                            g += p[1];
                            r += p[2];
                        }
                    }

                    const uint32_t count = static_cast<uint32_t>((x1 - x0) * (y1 - y0));
                    const uint32_t half = count / 2;
                    dst[0] = static_cast<uint8_t>((b + half) / count);
                    dst[1] = static_cast<uint8_t>((g + half) / count);
                    dst[2] = static_cast<uint8_t>((r + half) / count);
                    dst[3] = 255;
                }
            }
        });
        return;
    }

    std::vector<BilinearTap> columns;
    std::vector<BilinearTap> rows;
    BuildBilinearTaps(source.width, width, columns);
    BuildBilinearTaps(source.height, height, rows);

    ParallelRows(height, [&](int rowBegin, int rowEnd) {
        for (int y = rowBegin; y < rowEnd; ++y) {
            const BilinearTap& row = rows[static_cast<size_t>(y)];
            const uint8_t* top = src + static_cast<size_t>(row.first) * srcStride;
            const uint8_t* bottom = src + static_cast<size_t>(row.second) * srcStride;
            const int fy = row.weight;
            const int gy = 256 - fy;

            uint8_t* dst = out.pixels.data() + static_cast<size_t>(y) * out.stride();

            for (int x = 0; x < width; ++x, dst += 4) {
                const BilinearTap& column = columns[static_cast<size_t>(x)];
                const size_t left = static_cast<size_t>(column.first) * 4;
                const size_t right = static_cast<size_t>(column.second) * 4;
                const int fx = column.weight;
                const int gx = 256 - fx;

                for (int c = 0; c < 3; ++c) {
                    const int upper = top[left + c] * gx + top[right + c] * fx;
                    const int lower = bottom[left + c] * gx + bottom[right + c] * fx;
                    dst[c] = static_cast<uint8_t>((upper * gy + lower * fy + 32768) >> 16);
                }
                dst[3] = 255;
            }
        }
    });
}

class ImagePool : public std::enable_shared_from_this<ImagePool> {
public:
    explicit ImagePool(size_t keep) : keep_(keep) {}

    std::shared_ptr<Image> Acquire() {
        Image* image = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!free_.empty()) {
                image = free_.back().release();
                free_.pop_back();
            }
        }
        if (!image) image = new Image();

        const std::weak_ptr<ImagePool> owner = weak_from_this();
        return std::shared_ptr<Image>(image, [owner](Image* returned) {
            if (const std::shared_ptr<ImagePool> pool = owner.lock()) pool->Return(returned);
            else delete returned;
        });
    }

private:
    void Return(Image* image) {
        std::unique_ptr<Image> owned(image);
        std::lock_guard<std::mutex> lock(mutex_);
        if (free_.size() < keep_) free_.push_back(std::move(owned));
    }

    const size_t keep_;
    std::mutex mutex_;
    std::vector<std::unique_ptr<Image>> free_;
};

struct Compositor {
    Image scaled;

    bool Merge(const Image& base, const Image& overlay, Image& out, bool screenMerge, int lowKey, int highKey) {
        if (!base.valid() || !overlay.valid()) return false;

        const Image* top = &overlay;
        if (overlay.width != base.width || overlay.height != base.height) {
            ScaleToInto(overlay, scaled, base.width, base.height);
            if (!scaled.valid()) return false;
            top = &scaled;
        }

        if (out.width != base.width || out.height != base.height) out.allocate(base.width, base.height);

        if (highKey <= lowKey) highKey = lowKey + 1;

        const int width = base.width;
        const int range = highKey - lowKey;

        ParallelRows(base.height, [&](int rowBegin, int rowEnd) {
            for (int y = rowBegin; y < rowEnd; ++y) {
                const size_t offset = static_cast<size_t>(y) * base.stride();
                const uint8_t* b = base.pixels.data() + offset;
                const uint8_t* o = top->pixels.data() + offset;
                uint8_t* d = out.pixels.data() + offset;

                for (int x = 0; x < width; ++x, b += 4, o += 4, d += 4) {
                    if (screenMerge) {
                        for (int c = 0; c < 3; ++c) {
                            d[c] = static_cast<uint8_t>(255 - ((255 - b[c]) * (255 - o[c])) / 255);
                        }
                    } else {
                        const int luma = Luma(o);
                        if (luma <= lowKey) {
                            d[0] = b[0]; d[1] = b[1]; d[2] = b[2];
                        } else if (luma >= highKey) {
                            d[0] = o[0]; d[1] = o[1]; d[2] = o[2];
                        } else {
                            const int alpha = ((luma - lowKey) * 255) / range;
                            for (int c = 0; c < 3; ++c) {
                                d[c] = static_cast<uint8_t>((b[c] * (255 - alpha) + o[c] * alpha) / 255);
                            }
                        }
                    }
                    d[3] = 255;
                }
            }
        });

        return true;
    }
};

struct TimedFrame {
    std::shared_ptr<const Image> image;
    int64_t captureUs = 0;
    int64_t presentUs = 0;
    int64_t arrivedUs = 0;
    uint64_t serial = 0;
    size_t wireBytes = 0;

    bool valid() const { return image && image->valid(); }
};

class LatestFrame {
public:
    void Publish(TimedFrame frame) {
        std::lock_guard<std::mutex> lock(mutex_);
        frame.serial = ++serial_;
        frame_ = std::move(frame);
    }

    TimedFrame Get() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return frame_;
    }

    uint64_t serial() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return serial_;
    }

private:
    mutable std::mutex mutex_;
    TimedFrame frame_;
    uint64_t serial_ = 0;
};

class FrameRing {
public:
    explicit FrameRing(size_t capacity = 32) : entries_(capacity) {}

    void Push(TimedFrame frame) {
        std::lock_guard<std::mutex> lock(mutex_);
        frame.serial = ++serial_;
        entries_[next_] = std::move(frame);
        next_ = (next_ + 1) % entries_.size();
    }

    TimedFrame Nearest(int64_t wantUs) const {
        std::lock_guard<std::mutex> lock(mutex_);

        TimedFrame best;
        int64_t bestDistance = INT64_MAX;

        for (const TimedFrame& entry : entries_) {
            if (!entry.valid()) continue;

            int64_t distance = entry.captureUs - wantUs;
            if (distance < 0) distance = -distance;

            if (distance < bestDistance) {
                bestDistance = distance;
                best = entry;
            }
        }

        return best;
    }

    uint64_t serial() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return serial_;
    }

private:
    mutable std::mutex mutex_;
    std::vector<TimedFrame> entries_;
    size_t next_ = 0;
    uint64_t serial_ = 0;
};

class RateMeter {
public:
    void Add(size_t bytes = 0) {
        count_++;
        bytes_ += bytes;
    }

    void AddLatency(int64_t us) {
        latencyTotalUs_ += us;
        latencySamples_++;
    }

    bool Elapsed(int64_t nowUs, int64_t windowUs, double& perSecond, double& megabitsPerSecond, double& averageLatencyMs) {
        if (startUs_ == 0) {
            startUs_ = nowUs;
            return false;
        }

        const int64_t span = nowUs - startUs_;
        if (span < windowUs) return false;

        perSecond = static_cast<double>(count_) * 1000000.0 / static_cast<double>(span);
        megabitsPerSecond = static_cast<double>(bytes_) * 8.0 / static_cast<double>(span);
        averageLatencyMs = latencySamples_
            ? static_cast<double>(latencyTotalUs_) / static_cast<double>(latencySamples_) / 1000.0
            : -1.0;

        startUs_ = nowUs;
        count_ = 0;
        bytes_ = 0;
        latencyTotalUs_ = 0;
        latencySamples_ = 0;
        return true;
    }

private:
    int64_t startUs_ = 0;
    uint64_t count_ = 0;
    uint64_t bytes_ = 0;
    int64_t latencyTotalUs_ = 0;
    uint64_t latencySamples_ = 0;
};


class OutputWindow {
public:
    ~OutputWindow() { Destroy(); }

    bool Create(const std::wstring& title, int frameWidth, int frameHeight,
        const MonitorTarget* fullscreenOn, std::string& error) {

        Destroy();

        const HINSTANCE instance = GetModuleHandleW(nullptr);

        WNDCLASSEXW windowClass{ sizeof(WNDCLASSEXW) };
        windowClass.lpfnWndProc = &OutputWindow::WndProc;
        windowClass.hInstance = instance;
        windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        windowClass.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        windowClass.lpszClassName = kClassName;
        RegisterClassExW(&windowClass);

        if (fullscreenOn) {
            clientWidth_ = fullscreenOn->width();
            clientHeight_ = fullscreenOn->height();

            hwnd_ = CreateWindowExW(0, kClassName, title.c_str(), WS_POPUP,
                fullscreenOn->rect.left, fullscreenOn->rect.top,
                fullscreenOn->width(), fullscreenOn->height(),
                nullptr, nullptr, instance, this);
        } else {
            clientWidth_ = frameWidth;
            clientHeight_ = frameHeight;

            RECT chrome{ 0, 0, frameWidth, frameHeight };
            AdjustWindowRect(&chrome, WS_OVERLAPPEDWINDOW, FALSE);
            const int chromeWidth = (chrome.right - chrome.left) - frameWidth;
            const int chromeHeight = (chrome.bottom - chrome.top) - frameHeight;

            RECT work{};
            if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0)) {
                const int roomWidth = (work.right - work.left) - chromeWidth;
                const int roomHeight = (work.bottom - work.top) - chromeHeight;

                if (roomWidth > 64 && roomHeight > 64 && (frameWidth > roomWidth || frameHeight > roomHeight)) {
                    const double scale = std::min(
                        static_cast<double>(roomWidth) / frameWidth,
                        static_cast<double>(roomHeight) / frameHeight);
                    clientWidth_ = static_cast<int>(frameWidth * scale);
                    clientHeight_ = static_cast<int>(frameHeight * scale);
                }
            }

            RECT bounds{ 0, 0, clientWidth_, clientHeight_ };
            AdjustWindowRect(&bounds, WS_OVERLAPPEDWINDOW, FALSE);

            hwnd_ = CreateWindowExW(0, kClassName, title.c_str(), WS_OVERLAPPEDWINDOW,
                CW_USEDEFAULT, CW_USEDEFAULT,
                bounds.right - bounds.left, bounds.bottom - bounds.top,
                nullptr, nullptr, instance, this);
        }

        if (!hwnd_) {
            error = "cannot create the output window: " + ErrorText(GetLastError());
            return false;
        }

        ShowWindow(hwnd_, SW_SHOW);
        UpdateWindow(hwnd_);

        if (!CreateDevice(frameWidth, frameHeight, error)) {
            Destroy();
            return false;
        }

        return true;
    }

    void Destroy() {
        ReleaseSwapChain();

        if (context_) { context_->Release(); context_ = nullptr; }
        if (device_) { device_->Release(); device_ = nullptr; }
        if (hwnd_) { DestroyWindow(hwnd_); hwnd_ = nullptr; }

        clientWidth_ = clientHeight_ = 0;
        closed_ = false;
    }

    void Pump() {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_HOTKEY) {
                hotkeyHits_++;
                continue;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    bool TakeHotkey() {
        if (hotkeyHits_ == 0) return false;
        hotkeyHits_--;
        return true;
    }

    bool closed() const { return closed_; }
    HWND hwnd() const { return hwnd_; }

    int clientWidth() const { return clientWidth_; }
    int clientHeight() const { return clientHeight_; }

    bool Present(const Image& frame, std::string& error) {
        if (!swapChain_ || !frame.valid()) return false;

        if (frame.width != bufferWidth_ || frame.height != bufferHeight_) {
            if (!ResizeTo(frame.width, frame.height, error)) return false;
        }

        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context_->Map(upload_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            error = "cannot map the upload texture";
            return false;
        }

        const size_t rowBytes = frame.stride();
        uint8_t* destination = static_cast<uint8_t*>(mapped.pData);
        const uint8_t* source = frame.pixels.data();

        if (mapped.RowPitch == rowBytes) {
            memcpy(destination, source, rowBytes * frame.height);
        } else {
            for (int y = 0; y < frame.height; ++y) {
                memcpy(destination + static_cast<size_t>(y) * mapped.RowPitch, source + static_cast<size_t>(y) * rowBytes, rowBytes);
            }
        }

        context_->Unmap(upload_, 0);

        ID3D11Texture2D* backBuffer = nullptr;
        if (FAILED(swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuffer)))) {
            error = "cannot get the swap chain back buffer";
            return false;
        }

        context_->CopyResource(backBuffer, upload_);
        backBuffer->Release();

        const HRESULT hr = swapChain_->Present(0, 0);
        if (FAILED(hr)) {
            error = "Present failed - the display adapter may have been reset";
            return false;
        }

        return true;
    }

private:
    static constexpr const wchar_t* kClassName = L"ScreenFuse Output";

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
        if (message == WM_NCCREATE) {
            const CREATESTRUCTW* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        }

        OutputWindow* self = reinterpret_cast<OutputWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

        switch (message) {
        case WM_CLOSE:
            if (self) self->closed_ = true;
            return 0;
        case WM_KEYDOWN:
            if (self && wparam == VK_ESCAPE) self->closed_ = true;
            return 0;
        case WM_ERASEBKGND:
            return 1;
        default:
            break;
        }

        return DefWindowProcW(hwnd, message, wparam, lparam);
    }

    bool CreateDevice(int width, int height, std::string& error) {
        DXGI_SWAP_CHAIN_DESC desc{};
        desc.BufferCount = 2;
        desc.BufferDesc.Width = static_cast<UINT>(width);
        desc.BufferDesc.Height = static_cast<UINT>(height);
        desc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.OutputWindow = hwnd_;
        desc.SampleDesc.Count = 1;
        desc.Windowed = TRUE;
        desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };

        const HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
            &desc, &swapChain_, &device_, nullptr, &context_);

        if (FAILED(hr)) {
            error = "cannot create the output swap chain";
            return false;
        }

        bufferWidth_ = width;
        bufferHeight_ = height;

        IDXGIDevice* dxgiDevice = nullptr;
        if (SUCCEEDED(device_->QueryInterface(IID_PPV_ARGS(&dxgiDevice)))) {
            IDXGIAdapter* adapter = nullptr;
            if (SUCCEEDED(dxgiDevice->GetAdapter(&adapter))) {
                IDXGIFactory* factory = nullptr;
                if (SUCCEEDED(adapter->GetParent(IID_PPV_ARGS(&factory)))) {
                    factory->MakeWindowAssociation(hwnd_, DXGI_MWA_NO_ALT_ENTER);
                    factory->Release();
                }
                adapter->Release();
            }
            dxgiDevice->Release();
        }

        return CreateUploadTexture(width, height, error);
    }

    bool CreateUploadTexture(int width, int height, std::string& error) {
        if (upload_) { upload_->Release(); upload_ = nullptr; }

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = static_cast<UINT>(width);
        desc.Height = static_cast<UINT>(height);
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        if (FAILED(device_->CreateTexture2D(&desc, nullptr, &upload_))) {
            error = "cannot create the upload texture";
            return false;
        }

        return true;
    }

    bool ResizeTo(int width, int height, std::string& error) {
        if (FAILED(swapChain_->ResizeBuffers(0, static_cast<UINT>(width), static_cast<UINT>(height), DXGI_FORMAT_UNKNOWN, 0))) {
            error = "cannot resize the swap chain";
            return false;
        }

        bufferWidth_ = width;
        bufferHeight_ = height;
        return CreateUploadTexture(width, height, error);
    }

    void ReleaseSwapChain() {
        if (upload_) { upload_->Release(); upload_ = nullptr; }
        if (swapChain_) { swapChain_->Release(); swapChain_ = nullptr; }
        bufferWidth_ = bufferHeight_ = 0;
    }

    HWND hwnd_ = nullptr;
    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    IDXGISwapChain* swapChain_ = nullptr;
    ID3D11Texture2D* upload_ = nullptr;
    int bufferWidth_ = 0;
    int bufferHeight_ = 0;
    int clientWidth_ = 0;
    int clientHeight_ = 0;
    int hotkeyHits_ = 0;
    bool closed_ = false;
};

} // namespace screenfuse
