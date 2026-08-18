#pragma once

#include "common.hpp"
#include "image.hpp"

#include <d3d11.h>
#include <dxgi1_2.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

namespace screenfuse {

struct MonitorTarget {
    int index = 0;
    UINT adapterIndex = 0;
    UINT outputIndex = 0;
    std::wstring device;
    std::wstring adapter;
    RECT rect{};
    HMONITOR handle = nullptr;
    bool primary = false;

    int width() const { return rect.right - rect.left; }
    int height() const { return rect.bottom - rect.top; }
};

inline void EnableDpiAwareness() {
    using SetContextFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);

    if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
        if (auto setContext = reinterpret_cast<SetContextFn>(GetProcAddress(user32, "SetProcessDpiAwarenessContext"))) {
            if (setContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) return;
        }
    }

    SetProcessDPIAware();
}

inline std::vector<MonitorTarget> EnumerateMonitors() {
    std::vector<MonitorTarget> monitors;

    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return monitors;

    IDXGIAdapter1* adapter = nullptr;
    for (UINT adapterIndex = 0; factory->EnumAdapters1(adapterIndex, &adapter) != DXGI_ERROR_NOT_FOUND; ++adapterIndex) {
        DXGI_ADAPTER_DESC1 adapterDesc{};
        adapter->GetDesc1(&adapterDesc);

        IDXGIOutput* output = nullptr;
        for (UINT outputIndex = 0; adapter->EnumOutputs(outputIndex, &output) != DXGI_ERROR_NOT_FOUND; ++outputIndex) {
            DXGI_OUTPUT_DESC outputDesc{};
            if (SUCCEEDED(output->GetDesc(&outputDesc)) && outputDesc.AttachedToDesktop) {
                MONITORINFO info{ sizeof(MONITORINFO) };
                GetMonitorInfoW(outputDesc.Monitor, &info);

                MonitorTarget target;
                target.index = static_cast<int>(monitors.size());
                target.adapterIndex = adapterIndex;
                target.outputIndex = outputIndex;
                target.device = outputDesc.DeviceName;
                target.adapter = adapterDesc.Description;
                target.rect = outputDesc.DesktopCoordinates;
                target.handle = outputDesc.Monitor;
                target.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
                monitors.push_back(target);
            }

            output->Release();
            output = nullptr;
        }

        adapter->Release();
        adapter = nullptr;
    }

    factory->Release();
    return monitors;
}

inline const MonitorTarget* FindMonitorByHandle(const std::vector<MonitorTarget>& monitors, HMONITOR handle) {
    for (const MonitorTarget& monitor : monitors) {
        if (monitor.handle == handle) return &monitor;
    }
    return nullptr;
}

inline const MonitorTarget* FindMonitorByIndex(const std::vector<MonitorTarget>& monitors, int index) {
    for (const MonitorTarget& monitor : monitors) {
        if (monitor.index == index) return &monitor;
    }
    return nullptr;
}

inline const MonitorTarget* PrimaryMonitor(const std::vector<MonitorTarget>& monitors) {
    for (const MonitorTarget& monitor : monitors) {
        if (monitor.primary) return &monitor;
    }
    return monitors.empty() ? nullptr : &monitors.front();
}

struct CaptureOutput {
    Image image;
    int64_t presentTicks = 0;
    int64_t captureTicks = 0;
    bool usedGdi = false;
    bool late = false;
    int framesSeen = 0;
    uint32_t sourceFormat = 0;
};

inline const char* FormatName(uint32_t format) {
    switch (format) {
    case DXGI_FORMAT_B8G8R8A8_UNORM: return "B8G8R8A8_UNORM";
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return "B8G8R8A8_UNORM_SRGB";
    case DXGI_FORMAT_B8G8R8X8_UNORM: return "B8G8R8X8_UNORM";
    case DXGI_FORMAT_R8G8B8A8_UNORM: return "R8G8B8A8_UNORM";
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return "R8G8B8A8_UNORM_SRGB";
    case DXGI_FORMAT_R10G10B10A2_UNORM: return "R10G10B10A2_UNORM";
    case DXGI_FORMAT_R16G16B16A16_FLOAT: return "R16G16B16A16_FLOAT";
    case 0: return "n/a";
    default: return "unknown";
    }
}

inline bool CaptureGdi(const MonitorTarget& target, int64_t targetTicks, CaptureOutput& out, std::string& error) {
    const int width = target.width();
    const int height = target.height();

    if (width <= 0 || height <= 0) {
        error = "monitor has no usable size";
        return false;
    }

    HDC screen = CreateDCW(target.device.c_str(), nullptr, nullptr, nullptr);
    if (!screen) {
        error = "CreateDC failed for " + ToUtf8(target.device) + ": " + ErrorText(GetLastError());
        return false;
    }

    HDC memory = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, width, height);
    HGDIOBJ previous = SelectObject(memory, bitmap);

    WaitUntilTicks(targetTicks);

    const bool blitted = BitBlt(memory, 0, 0, width, height, screen, 0, 0, SRCCOPY | CAPTUREBLT) != FALSE;
    out.captureTicks = QpcTicks();

    bool ok = blitted;
    if (!ok) error = "BitBlt failed: " + ErrorText(GetLastError());

    if (ok) {
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;

        out.image.allocate(width, height);
        ok = GetDIBits(memory, bitmap, 0, static_cast<UINT>(height), out.image.pixels.data(), &info, DIB_RGB_COLORS) != 0;
        if (!ok) error = "GetDIBits failed: " + ErrorText(GetLastError());
    }

    if (ok) {
        for (size_t i = 3; i < out.image.pixels.size(); i += 4) out.image.pixels[i] = 255;
    }

    SelectObject(memory, previous);
    DeleteObject(bitmap);
    DeleteDC(memory);
    DeleteDC(screen);

    out.usedGdi = true;
    out.presentTicks = 0;
    return ok;
}

class DuplicationSession {
public:
    ~DuplicationSession() { Reset(); }

    bool Open(const MonitorTarget& target, std::string& error) {
        Reset();

        IDXGIFactory1* factory = nullptr;
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
            error = "CreateDXGIFactory1 failed";
            return false;
        }

        IDXGIAdapter1* adapter = nullptr;
        HRESULT hr = factory->EnumAdapters1(target.adapterIndex, &adapter);
        factory->Release();

        if (FAILED(hr) || !adapter) {
            error = "adapter " + std::to_string(target.adapterIndex) + " is gone";
            return false;
        }

        IDXGIOutput* output = nullptr;
        hr = adapter->EnumOutputs(target.outputIndex, &output);
        if (FAILED(hr) || !output) {
            adapter->Release();
            error = "output " + std::to_string(target.outputIndex) + " is gone";
            return false;
        }

        hr = output->QueryInterface(IID_PPV_ARGS(&output1_));
        output->Release();

        if (FAILED(hr)) {
            adapter->Release();
            error = "IDXGIOutput1 unavailable (Windows 8 or newer required)";
            return false;
        }

        const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1 };
        hr = D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0,
            levels, ARRAYSIZE(levels), D3D11_SDK_VERSION, &device_, nullptr, &context_);
        adapter->Release();

        if (FAILED(hr)) {
            error = "D3D11CreateDevice failed (hr=0x" + ToHex(reinterpret_cast<const uint8_t*>(&hr), sizeof(hr)) + ")";
            Reset();
            return false;
        }

        return Duplicate(error);
    }

    bool Duplicate(std::string& error) {
        if (duplication_) {
            duplication_->Release();
            duplication_ = nullptr;
        }

        const HRESULT hr = output1_->DuplicateOutput(device_, &duplication_);
        if (FAILED(hr)) {
            if (hr == DXGI_ERROR_NOT_CURRENTLY_AVAILABLE) {
                error = "desktop duplication is already in use by the maximum number of applications";
            } else if (hr == E_ACCESSDENIED) {
                error = "access denied - a secure desktop (UAC/lock screen) is up";
            } else if (hr == DXGI_ERROR_UNSUPPORTED) {
                error = "desktop duplication unsupported on this adapter/output";
            } else {
                error = "DuplicateOutput failed";
            }
            return false;
        }

        return true;
    }

    IDXGIOutputDuplication* duplication() const { return duplication_; }
    ID3D11Device* device() const { return device_; }
    ID3D11DeviceContext* context() const { return context_; }

    void Reset() {
        if (duplication_) { duplication_->Release(); duplication_ = nullptr; }
        if (output1_) { output1_->Release(); output1_ = nullptr; }
        if (context_) { context_->Release(); context_ = nullptr; }
        if (device_) { device_->Release(); device_ = nullptr; }
    }

private:
    IDXGIOutput1* output1_ = nullptr;
    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    IDXGIOutputDuplication* duplication_ = nullptr;
};

inline float HalfToFloat(uint16_t half) {
    const uint32_t sign = (half & 0x8000u) << 16;
    uint32_t exponent = (half >> 10) & 0x1Fu;
    uint32_t mantissa = half & 0x3FFu;

    if (exponent == 0) {
        if (mantissa == 0) {
            const uint32_t bits = sign;
            float value;
            memcpy(&value, &bits, sizeof(value));
            return value;
        }
        while ((mantissa & 0x400u) == 0) {
            mantissa <<= 1;
            exponent--;
        }
        exponent++;
        mantissa &= 0x3FFu;
    } else if (exponent == 0x1Fu) {
        const uint32_t bits = sign | 0x7F800000u | (mantissa << 13);
        float value;
        memcpy(&value, &bits, sizeof(value));
        return value;
    }

    const uint32_t bits = sign | ((exponent + 112u) << 23) | (mantissa << 13);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

inline uint8_t LinearToSrgbByte(float value) {
    if (!(value > 0.0f)) return 0;
    if (value >= 1.0f) return 255;

    const float encoded = value <= 0.0031308f
        ? value * 12.92f
        : 1.055f * powf(value, 1.0f / 2.4f) - 0.055f;

    return static_cast<uint8_t>(encoded * 255.0f + 0.5f);
}

inline bool ConvertRow(const uint8_t* source, uint8_t* dest, int width, DXGI_FORMAT format) {
    switch (format) {
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8X8_UNORM:
        memcpy(dest, source, static_cast<size_t>(width) * 4);
        return true;

    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        for (int x = 0; x < width; ++x) {
            dest[x * 4 + 0] = source[x * 4 + 2];
            dest[x * 4 + 1] = source[x * 4 + 1];
            dest[x * 4 + 2] = source[x * 4 + 0];
            dest[x * 4 + 3] = source[x * 4 + 3];
        }
        return true;

    case DXGI_FORMAT_R10G10B10A2_UNORM:
        for (int x = 0; x < width; ++x) {
            uint32_t packed;
            memcpy(&packed, source + x * 4, sizeof(packed));
            dest[x * 4 + 0] = static_cast<uint8_t>(((packed >> 20) & 0x3FF) >> 2);
            dest[x * 4 + 1] = static_cast<uint8_t>(((packed >> 10) & 0x3FF) >> 2);
            dest[x * 4 + 2] = static_cast<uint8_t>((packed & 0x3FF) >> 2);
            dest[x * 4 + 3] = 255;
        }
        return true;

    case DXGI_FORMAT_R16G16B16A16_FLOAT:
        for (int x = 0; x < width; ++x) {
            uint16_t channels[4];
            memcpy(channels, source + x * 8, sizeof(channels));
            dest[x * 4 + 0] = LinearToSrgbByte(HalfToFloat(channels[2]));
            dest[x * 4 + 1] = LinearToSrgbByte(HalfToFloat(channels[1]));
            dest[x * 4 + 2] = LinearToSrgbByte(HalfToFloat(channels[0]));
            dest[x * 4 + 3] = 255;
        }
        return true;

    default:
        return false;
    }
}

inline bool ReadbackTexture(ID3D11Device* device, ID3D11DeviceContext* context,
    ID3D11Texture2D* source, Image& out, std::string& error) {

    D3D11_TEXTURE2D_DESC desc{};
    source->GetDesc(&desc);

    D3D11_TEXTURE2D_DESC stagingDesc = desc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.MiscFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    ID3D11Texture2D* staging = nullptr;
    if (FAILED(device->CreateTexture2D(&stagingDesc, nullptr, &staging))) {
        error = "cannot create staging texture";
        return false;
    }

    context->CopyResource(staging, source);

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) {
        staging->Release();
        error = "cannot map staging texture";
        return false;
    }

    out.allocate(static_cast<int>(desc.Width), static_cast<int>(desc.Height));

    const uint8_t* sourceRow = static_cast<const uint8_t*>(mapped.pData);
    bool ok = true;

    for (int y = 0; y < out.height && ok; ++y) {
        ok = ConvertRow(sourceRow, out.pixels.data() + static_cast<size_t>(y) * out.stride(), out.width, desc.Format);
        sourceRow += mapped.RowPitch;
    }

    context->Unmap(staging, 0);
    staging->Release();

    if (!ok) {
        error = std::string("unsupported desktop surface format ") + FormatName(desc.Format) +
            " (" + std::to_string(static_cast<int>(desc.Format)) + ")";
        return false;
    }

    for (size_t i = 3; i < out.pixels.size(); i += 4) out.pixels[i] = 255;

    return true;
}

inline bool CaptureAtTicks(const MonitorTarget& target, int64_t targetTicks, CaptureOutput& out, std::string& error) {
    out = CaptureOutput{};
    out.late = QpcTicks() > targetTicks;

    DuplicationSession session;
    if (!session.Open(target, error)) {
        std::string gdiError;
        if (CaptureGdi(target, targetTicks, out, gdiError)) {
            error = "desktop duplication unavailable (" + error + "), used GDI instead";
            return true;
        }
        error += "; GDI fallback also failed: " + gdiError;
        return false;
    }

    ID3D11Texture2D* keep = nullptr;
    int64_t presentTicks = 0;
    bool haveFrame = false;
    int framesSeen = 0;
    int accessLostRetries = 0;

    for (;;) {
        const int64_t now = QpcTicks();
        if (now >= targetTicks && haveFrame) break;

        int64_t remainUs = TicksToUs(targetTicks - now);
        if (remainUs < 0) remainUs = 0;

        UINT timeoutMs = static_cast<UINT>(remainUs / 1000);
        if (timeoutMs > 8) timeoutMs = 8;
        if (!haveFrame && timeoutMs < 4) timeoutMs = 4;

        DXGI_OUTDUPL_FRAME_INFO info{};
        IDXGIResource* resource = nullptr;
        const HRESULT hr = session.duplication()->AcquireNextFrame(timeoutMs, &info, &resource);

        if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
            if (!haveFrame && QpcTicks() > targetTicks + UsToTicks(500000)) {
                error = "no frame arrived within 500ms of the target instant";
                break;
            }
            continue;
        }

        if (hr == DXGI_ERROR_ACCESS_LOST) {
            if (++accessLostRetries > 3 || !session.Duplicate(error)) {
                error = "duplication lost (display mode change or fullscreen transition)";
                break;
            }
            continue;
        }

        if (FAILED(hr) || !resource) {
            error = "AcquireNextFrame failed";
            break;
        }

        ID3D11Texture2D* texture = nullptr;
        if (SUCCEEDED(resource->QueryInterface(IID_PPV_ARGS(&texture)))) {
            if (!keep) {
                D3D11_TEXTURE2D_DESC desc{};
                texture->GetDesc(&desc);
                out.sourceFormat = static_cast<uint32_t>(desc.Format);
                desc.Usage = D3D11_USAGE_DEFAULT;
                desc.BindFlags = 0;
                desc.CPUAccessFlags = 0;
                desc.MiscFlags = 0;
                session.device()->CreateTexture2D(&desc, nullptr, &keep);
            }

            if (keep) {
                session.context()->CopyResource(keep, texture);
                haveFrame = true;
                framesSeen++;
                if (info.LastPresentTime.QuadPart != 0) presentTicks = info.LastPresentTime.QuadPart;
            }

            texture->Release();
        }

        resource->Release();
        session.duplication()->ReleaseFrame();
    }

    bool ok = false;

    if (haveFrame && keep) {
        out.captureTicks = QpcTicks();
        out.presentTicks = presentTicks;
        out.framesSeen = framesSeen;
        ok = ReadbackTexture(session.device(), session.context(), keep, out.image, error);
    }

    if (keep) keep->Release();

    if (!ok) {
        std::string gdiError;
        CaptureOutput fallback;
        if (CaptureGdi(target, QpcTicks(), fallback, gdiError)) {
            fallback.late = out.late;
            out = fallback;
            error = "duplication failed (" + error + "), used GDI instead";
            return true;
        }
        if (error.empty()) error = gdiError;
        return false;
    }

    return true;
}


class ContinuousCapture {
public:
    ~ContinuousCapture() { Close(); }

    bool Open(const MonitorTarget& target, std::string& error) {
        Close();
        target_ = target;

        if (session_.Open(target, error)) {
            gdi_ = false;
            return true;
        }

        CaptureOutput probe;
        std::string gdiError;
        if (!CaptureGdi(target, QpcTicks(), probe, gdiError)) {
            error += "; GDI fallback also failed: " + gdiError;
            return false;
        }

        error = "desktop duplication unavailable (" + error + "), streaming over GDI instead - no present timestamps";
        gdi_ = true;
        return true;
    }

    void Close() {
        DropStaging();
        session_.Reset();
        gdi_ = false;
    }

    bool Next(int timeoutMs, Image& out, int64_t& presentTicks, int64_t& captureTicks, bool& fresh, std::string& error) {
        fresh = false;
        presentTicks = 0;

        if (gdi_) {
            if (timeoutMs > 0) Sleep(static_cast<DWORD>(timeoutMs));

            CaptureOutput capture;
            if (!CaptureGdi(target_, QpcTicks(), capture, error)) return false;

            out = std::move(capture.image);
            captureTicks = capture.captureTicks;
            fresh = true;
            return true;
        }

        DXGI_OUTDUPL_FRAME_INFO info{};
        IDXGIResource* resource = nullptr;
        const HRESULT hr = session_.duplication()->AcquireNextFrame(
            static_cast<UINT>(timeoutMs < 0 ? 0 : timeoutMs), &info, &resource);

        if (hr == DXGI_ERROR_WAIT_TIMEOUT) return true;

        if (hr == DXGI_ERROR_ACCESS_LOST) {
            if (resource) resource->Release();
            if (!session_.Duplicate(error)) {
                DropStaging();
                return session_.Open(target_, error);
            }
            return true;
        }

        if (FAILED(hr) || !resource) {
            error = "AcquireNextFrame failed";
            return false;
        }

        ID3D11Texture2D* texture = nullptr;
        bool ok = SUCCEEDED(resource->QueryInterface(IID_PPV_ARGS(&texture))) && texture != nullptr;
        if (ok) {
            ok = ReadInto(texture, out, error);
            captureTicks = QpcTicks();
            presentTicks = info.LastPresentTime.QuadPart;
            texture->Release();
        } else {
            error = "duplication handed back something that is not a texture";
        }

        resource->Release();
        session_.duplication()->ReleaseFrame();

        fresh = ok;
        return ok;
    }

    bool usedGdi() const { return gdi_; }
    uint32_t sourceFormat() const { return sourceFormat_; }

private:
    void DropStaging() {
        if (staging_) { staging_->Release(); staging_ = nullptr; }
        stagingWidth_ = stagingHeight_ = 0;
        stagingFormat_ = DXGI_FORMAT_UNKNOWN;
    }

    bool ReadInto(ID3D11Texture2D* texture, Image& out, std::string& error) {
        D3D11_TEXTURE2D_DESC desc{};
        texture->GetDesc(&desc);
        sourceFormat_ = static_cast<uint32_t>(desc.Format);

        if (!staging_ || desc.Width != stagingWidth_ || desc.Height != stagingHeight_ || desc.Format != stagingFormat_) {
            if (staging_) { staging_->Release(); staging_ = nullptr; }

            D3D11_TEXTURE2D_DESC stagingDesc = desc;
            stagingDesc.Usage = D3D11_USAGE_STAGING;
            stagingDesc.BindFlags = 0;
            stagingDesc.MiscFlags = 0;
            stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

            if (FAILED(session_.device()->CreateTexture2D(&stagingDesc, nullptr, &staging_))) {
                error = "cannot create the staging texture";
                return false;
            }

            stagingWidth_ = desc.Width;
            stagingHeight_ = desc.Height;
            stagingFormat_ = desc.Format;
        }

        session_.context()->CopyResource(staging_, texture);

        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(session_.context()->Map(staging_, 0, D3D11_MAP_READ, 0, &mapped))) {
            error = "cannot map the staging texture";
            return false;
        }

        if (out.width != static_cast<int>(desc.Width) || out.height != static_cast<int>(desc.Height)) {
            out.allocate(static_cast<int>(desc.Width), static_cast<int>(desc.Height));
        }

        const uint8_t* sourceRow = static_cast<const uint8_t*>(mapped.pData);
        bool ok = true;

        for (int y = 0; y < out.height && ok; ++y) {
            ok = ConvertRow(sourceRow, out.pixels.data() + static_cast<size_t>(y) * out.stride(), out.width, desc.Format);
            sourceRow += mapped.RowPitch;
        }

        session_.context()->Unmap(staging_, 0);

        if (!ok) {
            error = std::string("unsupported desktop surface format ") + FormatName(desc.Format);
            return false;
        }

        for (size_t i = 3; i < out.pixels.size(); i += 4) out.pixels[i] = 255;
        return true;
    }

    MonitorTarget target_{};
    DuplicationSession session_;
    ID3D11Texture2D* staging_ = nullptr;
    UINT stagingWidth_ = 0;
    UINT stagingHeight_ = 0;
    DXGI_FORMAT stagingFormat_ = DXGI_FORMAT_UNKNOWN;
    bool gdi_ = false;
    uint32_t sourceFormat_ = 0;
};

} // namespace screenfuse
