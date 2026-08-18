#pragma once

#include "common.hpp"

#include <wincodec.h>
#include <objbase.h>

#include <algorithm>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

namespace screenfuse {

struct Image {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;

    bool valid() const { return width > 0 && height > 0 && pixels.size() == static_cast<size_t>(width) * height * 4; }
    size_t stride() const { return static_cast<size_t>(width) * 4; }

    void allocate(int w, int h) {
        width = w;
        height = h;
        pixels.assign(static_cast<size_t>(w) * h * 4, 0);
    }
};

struct ComScope {
    bool ok = false;

    ComScope() {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        ok = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
    }

    ~ComScope() {
        if (ok) CoUninitialize();
    }
};

inline int Luma(const uint8_t* bgra) {
    return (19 * bgra[0] + 183 * bgra[1] + 54 * bgra[2]) >> 8;
}

struct ImageStats {
    double blackRatio = 0.0;
    bool uniform = false;
};

inline ImageStats Measure(const Image& image, int lumaThreshold) {
    ImageStats stats;
    if (!image.valid()) return stats;

    const size_t total = static_cast<size_t>(image.width) * image.height;
    size_t dark = 0;

    const uint32_t first = *reinterpret_cast<const uint32_t*>(image.pixels.data());
    bool uniform = true;

    for (size_t i = 0; i < total; ++i) {
        const uint8_t* p = image.pixels.data() + i * 4;
        if (Luma(p) <= lumaThreshold) dark++;
        if (uniform && *reinterpret_cast<const uint32_t*>(p) != first) uniform = false;
    }

    stats.blackRatio = static_cast<double>(dark) / static_cast<double>(total);
    stats.uniform = uniform;
    return stats;
}

struct KeyBreakdown {
    double keyedOut = 0.0;
    double soft = 0.0;
    double opaque = 0.0;
};

inline KeyBreakdown MeasureKey(const Image& image, int lowKey, int highKey) {
    KeyBreakdown out;
    if (!image.valid()) return out;

    if (highKey <= lowKey) highKey = lowKey + 1;

    const size_t total = static_cast<size_t>(image.width) * image.height;
    size_t keyedOut = 0, soft = 0, opaque = 0;

    for (size_t i = 0; i < total; ++i) {
        const int luma = Luma(image.pixels.data() + i * 4);
        if (luma <= lowKey) keyedOut++;
        else if (luma >= highKey) opaque++;
        else soft++;
    }

    out.keyedOut = static_cast<double>(keyedOut) / static_cast<double>(total);
    out.soft = static_cast<double>(soft) / static_cast<double>(total);
    out.opaque = static_cast<double>(opaque) / static_cast<double>(total);
    return out;
}

inline Image ScaleTo(const Image& source, int width, int height) {
    if (!source.valid() || width <= 0 || height <= 0) return {};
    if (source.width == width && source.height == height) return source;

    Image out;
    out.allocate(width, height);

    const double xRatio = static_cast<double>(source.width) / width;
    const double yRatio = static_cast<double>(source.height) / height;

    for (int y = 0; y < height; ++y) {
        const double sy = (y + 0.5) * yRatio - 0.5;
        const int y0 = std::max(0, static_cast<int>(sy));
        const int y1 = std::min(source.height - 1, y0 + 1);
        const double fy = std::max(0.0, sy - y0);

        for (int x = 0; x < width; ++x) {
            const double sx = (x + 0.5) * xRatio - 0.5;
            const int x0 = std::max(0, static_cast<int>(sx));
            const int x1 = std::min(source.width - 1, x0 + 1);
            const double fx = std::max(0.0, sx - x0);

            const uint8_t* p00 = source.pixels.data() + (static_cast<size_t>(y0) * source.width + x0) * 4;
            const uint8_t* p01 = source.pixels.data() + (static_cast<size_t>(y0) * source.width + x1) * 4;
            const uint8_t* p10 = source.pixels.data() + (static_cast<size_t>(y1) * source.width + x0) * 4;
            const uint8_t* p11 = source.pixels.data() + (static_cast<size_t>(y1) * source.width + x1) * 4;

            uint8_t* dst = out.pixels.data() + (static_cast<size_t>(y) * width + x) * 4;

            for (int c = 0; c < 4; ++c) {
                const double top = p00[c] + (p01[c] - p00[c]) * fx;
                const double bottom = p10[c] + (p11[c] - p10[c]) * fx;
                dst[c] = static_cast<uint8_t>(top + (bottom - top) * fy + 0.5);
            }
        }
    }

    return out;
}

inline Image MergeLumaKey(const Image& base, const Image& overlay, int lowKey, int highKey) {
    if (!base.valid() || !overlay.valid()) return {};

    const Image scaled = ScaleTo(overlay, base.width, base.height);
    if (!scaled.valid()) return {};

    if (highKey <= lowKey) highKey = lowKey + 1;

    Image out;
    out.allocate(base.width, base.height);

    const size_t total = static_cast<size_t>(base.width) * base.height;

    for (size_t i = 0; i < total; ++i) {
        const uint8_t* b = base.pixels.data() + i * 4;
        const uint8_t* o = scaled.pixels.data() + i * 4;
        uint8_t* d = out.pixels.data() + i * 4;

        const int luma = Luma(o);
        int alpha;
        if (luma <= lowKey) alpha = 0;
        else if (luma >= highKey) alpha = 255;
        else alpha = ((luma - lowKey) * 255) / (highKey - lowKey);

        for (int c = 0; c < 3; ++c) {
            d[c] = static_cast<uint8_t>((b[c] * (255 - alpha) + o[c] * alpha) / 255);
        }
        d[3] = 255;
    }

    return out;
}

inline Image MergeScreen(const Image& base, const Image& overlay) {
    if (!base.valid() || !overlay.valid()) return {};

    const Image scaled = ScaleTo(overlay, base.width, base.height);
    if (!scaled.valid()) return {};

    Image out;
    out.allocate(base.width, base.height);

    const size_t total = static_cast<size_t>(base.width) * base.height;

    for (size_t i = 0; i < total; ++i) {
        const uint8_t* b = base.pixels.data() + i * 4;
        const uint8_t* o = scaled.pixels.data() + i * 4;
        uint8_t* d = out.pixels.data() + i * 4;

        for (int c = 0; c < 3; ++c) {
            d[c] = static_cast<uint8_t>(255 - ((255 - b[c]) * (255 - o[c])) / 255);
        }
        d[3] = 255;
    }

    return out;
}

inline bool CreateWicFactory(IWICImagingFactory** factory) {
    return SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(factory)));
}

struct WicFactory {
    IWICImagingFactory* factory = nullptr;

    WicFactory() { CreateWicFactory(&factory); }
    ~WicFactory() { if (factory) factory->Release(); }

    WicFactory(const WicFactory&) = delete;
    WicFactory& operator=(const WicFactory&) = delete;

    bool ok() const { return factory != nullptr; }
    IWICImagingFactory* get() const { return factory; }
};

inline bool EncodePngToStream(IWICImagingFactory* factory, IStream* stream, const Image& image, std::string& error) {
    IWICBitmapEncoder* encoder = nullptr;
    if (FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder))) {
        error = "WIC: cannot create PNG encoder";
        return false;
    }

    bool ok = SUCCEEDED(encoder->Initialize(stream, WICBitmapEncoderNoCache));

    IWICBitmapFrameEncode* frame = nullptr;
    IPropertyBag2* properties = nullptr;
    if (ok) ok = SUCCEEDED(encoder->CreateNewFrame(&frame, &properties));
    if (ok) ok = SUCCEEDED(frame->Initialize(properties));
    if (ok) ok = SUCCEEDED(frame->SetSize(static_cast<UINT>(image.width), static_cast<UINT>(image.height)));

    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (ok) ok = SUCCEEDED(frame->SetPixelFormat(&format));

    IWICBitmap* source = nullptr;
    if (ok) {
        ok = SUCCEEDED(factory->CreateBitmapFromMemory(
            static_cast<UINT>(image.width),
            static_cast<UINT>(image.height),
            GUID_WICPixelFormat32bppBGRA,
            static_cast<UINT>(image.stride()),
            static_cast<UINT>(image.pixels.size()),
            const_cast<BYTE*>(image.pixels.data()),
            &source));
        if (!ok) error = "WIC: cannot wrap the pixel buffer";
    }

    if (ok) ok = SUCCEEDED(frame->WriteSource(source, nullptr));
    if (ok) ok = SUCCEEDED(frame->Commit());
    if (ok) ok = SUCCEEDED(encoder->Commit());

    if (source) source->Release();
    if (properties) properties->Release();
    if (frame) frame->Release();
    if (encoder) encoder->Release();

    if (!ok && error.empty()) error = "WIC: PNG encode failed";
    return ok;
}

inline bool SavePng(const std::wstring& path, const Image& image, std::string& error) {
    if (!image.valid()) {
        error = "refusing to save an invalid image";
        return false;
    }

    IWICImagingFactory* factory = nullptr;
    if (!CreateWicFactory(&factory)) {
        error = "WIC: cannot create imaging factory";
        return false;
    }

    IWICStream* stream = nullptr;
    bool ok = SUCCEEDED(factory->CreateStream(&stream));
    if (ok) ok = SUCCEEDED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE));
    if (!ok) error = "WIC: cannot open " + ToUtf8(path) + " for writing";

    if (ok) ok = EncodePngToStream(factory, stream, image, error);

    if (stream) stream->Release();
    factory->Release();
    return ok;
}

inline bool EncodePng(const Image& image, std::vector<uint8_t>& out, std::string& error) {
    if (!image.valid()) {
        error = "refusing to encode an invalid image";
        return false;
    }

    IWICImagingFactory* factory = nullptr;
    if (!CreateWicFactory(&factory)) {
        error = "WIC: cannot create imaging factory";
        return false;
    }

    IStream* stream = nullptr;
    bool ok = SUCCEEDED(CreateStreamOnHGlobal(nullptr, TRUE, &stream));
    if (ok) ok = EncodePngToStream(factory, stream, image, error);

    if (ok) {
        HGLOBAL memory = nullptr;
        ok = SUCCEEDED(GetHGlobalFromStream(stream, &memory));
        if (ok) {
            const SIZE_T size = GlobalSize(memory);
            const void* data = GlobalLock(memory);
            if (data) {
                out.assign(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + size);
                GlobalUnlock(memory);
            } else {
                ok = false;
                error = "WIC: cannot lock encoded PNG";
            }
        }
    }

    if (stream) stream->Release();
    factory->Release();
    return ok;
}

inline bool EncodeJpeg(IWICImagingFactory* factory, const Image& image, int quality,
    std::vector<uint8_t>& out, std::string& error) {

    if (!factory || !image.valid()) {
        error = "refusing to encode an invalid image";
        return false;
    }

    if (quality < 1) quality = 1;
    if (quality > 100) quality = 100;

    IStream* stream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream))) {
        error = "WIC: cannot create a memory stream";
        return false;
    }

    IWICBitmapEncoder* encoder = nullptr;
    bool ok = SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, &encoder));
    if (!ok) error = "WIC: cannot create the JPEG encoder";

    if (ok) ok = SUCCEEDED(encoder->Initialize(stream, WICBitmapEncoderNoCache));

    IWICBitmapFrameEncode* frame = nullptr;
    IPropertyBag2* properties = nullptr;
    if (ok) ok = SUCCEEDED(encoder->CreateNewFrame(&frame, &properties));

    if (ok && properties) {
        PROPBAG2 option{};
        option.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
        VARIANT value{};
        value.vt = VT_R4;
        value.fltVal = static_cast<float>(quality) / 100.0f;
        properties->Write(1, &option, &value);
    }

    if (ok) ok = SUCCEEDED(frame->Initialize(properties));
    if (ok) ok = SUCCEEDED(frame->SetSize(static_cast<UINT>(image.width), static_cast<UINT>(image.height)));

    WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
    if (ok) ok = SUCCEEDED(frame->SetPixelFormat(&format));

    IWICBitmap* source = nullptr;
    if (ok) {
        ok = SUCCEEDED(factory->CreateBitmapFromMemory(
            static_cast<UINT>(image.width),
            static_cast<UINT>(image.height),
            GUID_WICPixelFormat32bppBGRA,
            static_cast<UINT>(image.stride()),
            static_cast<UINT>(image.pixels.size()),
            const_cast<BYTE*>(image.pixels.data()),
            &source));
        if (!ok) error = "WIC: cannot wrap the pixel buffer";
    }

    IWICFormatConverter* converter = nullptr;
    if (ok) ok = SUCCEEDED(factory->CreateFormatConverter(&converter));
    if (ok) ok = SUCCEEDED(converter->Initialize(source, format, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom));

    if (ok) ok = SUCCEEDED(frame->WriteSource(converter, nullptr));
    if (ok) ok = SUCCEEDED(frame->Commit());
    if (ok) ok = SUCCEEDED(encoder->Commit());

    if (ok) {
        HGLOBAL memory = nullptr;
        ok = SUCCEEDED(GetHGlobalFromStream(stream, &memory));
        if (ok) {
            LARGE_INTEGER zero{};
            ULARGE_INTEGER position{};
            stream->Seek(zero, STREAM_SEEK_CUR, &position);

            SIZE_T size = GlobalSize(memory);
            if (position.QuadPart > 0 && position.QuadPart < size) size = static_cast<SIZE_T>(position.QuadPart);

            const void* data = GlobalLock(memory);
            if (data) {
                out.assign(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + size);
                GlobalUnlock(memory);
            } else {
                ok = false;
                error = "WIC: cannot lock the encoded JPEG";
            }
        }
    }

    if (converter) converter->Release();
    if (source) source->Release();
    if (properties) properties->Release();
    if (frame) frame->Release();
    if (encoder) encoder->Release();
    if (stream) stream->Release();

    if (!ok && error.empty()) error = "WIC: JPEG encode failed";
    return ok;
}

inline bool DecodeImageBytes(IWICImagingFactory* factory, const uint8_t* data, size_t length,
    Image& out, std::string& error) {

    if (!factory) {
        error = "WIC: no imaging factory";
        return false;
    }

    IWICStream* stream = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;

    bool ok = SUCCEEDED(factory->CreateStream(&stream));
    if (ok) ok = SUCCEEDED(stream->InitializeFromMemory(const_cast<BYTE*>(data), static_cast<DWORD>(length)));
    if (ok) ok = SUCCEEDED(factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &decoder));
    if (ok) ok = SUCCEEDED(decoder->GetFrame(0, &frame));
    if (ok) ok = SUCCEEDED(factory->CreateFormatConverter(&converter));
    if (ok) ok = SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppBGRA,
        WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom));

    UINT width = 0, height = 0;
    if (ok) ok = SUCCEEDED(converter->GetSize(&width, &height));

    if (ok && (width == 0 || height == 0)) {
        ok = false;
        error = "image decoded to nothing";
    }

    if (ok) {
        out.allocate(static_cast<int>(width), static_cast<int>(height));
        ok = SUCCEEDED(converter->CopyPixels(nullptr,
            static_cast<UINT>(out.stride()),
            static_cast<UINT>(out.pixels.size()),
            out.pixels.data()));
    }

    if (converter) converter->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    if (stream) stream->Release();

    if (!ok && error.empty()) error = "WIC: decode failed";
    return ok;
}

inline bool ReadFileBytes(const std::wstring& path, std::vector<uint8_t>& out, std::string& error) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = "cannot open " + ToUtf8(path) + ": " + ErrorText(GetLastError());
        return false;
    }

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > 512ll * 1024 * 1024) {
        CloseHandle(file);
        error = "bad file size for " + ToUtf8(path);
        return false;
    }

    out.resize(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    const bool ok = ReadFile(file, out.data(), static_cast<DWORD>(out.size()), &read, nullptr) != FALSE && read == out.size();
    CloseHandle(file);

    if (!ok) error = "cannot read " + ToUtf8(path);
    return ok;
}

inline bool DecodePng(const uint8_t* data, size_t length, Image& out, std::string& error) {
    IWICImagingFactory* factory = nullptr;
    if (!CreateWicFactory(&factory)) {
        error = "WIC: cannot create imaging factory";
        return false;
    }

    IWICStream* stream = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;

    bool ok = SUCCEEDED(factory->CreateStream(&stream));
    if (ok) ok = SUCCEEDED(stream->InitializeFromMemory(const_cast<BYTE*>(data), static_cast<DWORD>(length)));
    if (ok) ok = SUCCEEDED(factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder));
    if (ok) ok = SUCCEEDED(decoder->GetFrame(0, &frame));
    if (ok) ok = SUCCEEDED(factory->CreateFormatConverter(&converter));
    if (ok) ok = SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppBGRA,
        WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom));

    UINT width = 0, height = 0;
    if (ok) ok = SUCCEEDED(converter->GetSize(&width, &height));

    if (ok && (width == 0 || height == 0)) {
        ok = false;
        error = "PNG decoded to an empty image";
    }

    if (ok) {
        out.allocate(static_cast<int>(width), static_cast<int>(height));
        ok = SUCCEEDED(converter->CopyPixels(nullptr,
            static_cast<UINT>(out.stride()),
            static_cast<UINT>(out.pixels.size()),
            out.pixels.data()));
    }

    if (converter) converter->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    if (stream) stream->Release();
    factory->Release();

    if (!ok && error.empty()) error = "WIC: PNG decode failed";
    return ok;
}

inline bool LoadPng(const std::wstring& path, Image& out, std::string& error) {
    std::vector<uint8_t> bytes;
    if (!ReadFileBytes(path, bytes, error)) return false;
    return DecodePng(bytes.data(), bytes.size(), out, error);
}

} // namespace screenfuse
