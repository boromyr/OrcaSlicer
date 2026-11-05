#include "ShellThumbnail.hpp"

#ifdef _WIN32

#include <windows.h>
#include <objbase.h>
#include <shobjidl.h>
#include <wincodec.h>

namespace Slic3r { namespace GUI {

namespace {

// Scoped COM pointer, so the early exits below cannot leak.
template<class T> struct ComPtr
{
    T *p = nullptr;
    ComPtr() = default;
    ComPtr(const ComPtr &) = delete;
    ComPtr &operator=(const ComPtr &) = delete;
    ~ComPtr() { if (p != nullptr) p->Release(); }
    T **operator&() { return &p; }
    T *operator->() const { return p; }
    operator T *() const { return p; }
};

// COM has to be live on whichever thread asks the shell for a thumbnail. S_FALSE (already
// initialized) still has to be balanced, RPC_E_CHANGED_MODE means someone else initialized
// this thread in another apartment and owns the uninitialize.
struct ComInitializer
{
    bool owned = false;
    ComInitializer() { owned = SUCCEEDED(::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)); }
    ComInitializer(const ComInitializer &) = delete;
    ComInitializer &operator=(const ComInitializer &) = delete;
    ~ComInitializer() { if (owned) ::CoUninitialize(); }
};

std::string encode_png(IWICImagingFactory *factory, IWICBitmapSource *source)
{
    ComPtr<IStream> stream;
    if (FAILED(::CreateStreamOnHGlobal(nullptr, TRUE, &stream)))
        return {};

    ComPtr<IWICBitmapEncoder> encoder;
    if (FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) ||
        FAILED(encoder->Initialize(stream, WICBitmapEncoderNoCache)))
        return {};

    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2>         options;
    if (FAILED(encoder->CreateNewFrame(&frame, &options)) ||
        FAILED(frame->Initialize(options)) ||
        FAILED(frame->WriteSource(source, nullptr)) ||
        FAILED(frame->Commit()) ||
        FAILED(encoder->Commit()))
        return {};

    // The HGLOBAL behind the stream is grown in chunks, so its size is not the payload size.
    STATSTG stat = {};
    if (FAILED(stream->Stat(&stat, STATFLAG_NONAME)) || stat.cbSize.QuadPart == 0 ||
        stat.cbSize.QuadPart > 32 * 1024 * 1024)
        return {};

    HGLOBAL handle = nullptr;
    if (FAILED(::GetHGlobalFromStream(stream, &handle)) || handle == nullptr)
        return {};

    std::string png;
    if (const void *data = ::GlobalLock(handle)) {
        png.assign(static_cast<const char *>(data), static_cast<size_t>(stat.cbSize.QuadPart));
        ::GlobalUnlock(handle);
    }
    return png;
}

} // namespace

std::string get_shell_thumbnail_png(const std::wstring &path, int size, bool cached_only)
{
    if (path.empty() || size <= 0)
        return {};

    ComInitializer com;

    ComPtr<IShellItemImageFactory> image_factory;
    if (FAILED(::SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&image_factory))))
        return {};

    // SIIGBF_THUMBNAILONLY keeps the shell from falling back to the generic file-type icon,
    // which would look like a broken preview in the recent file list.
    DWORD flags = SIIGBF_THUMBNAILONLY;
    if (cached_only)
        flags |= SIIGBF_INCACHEONLY;

    HBITMAP hbitmap = nullptr;
    SIZE    extent { size, size };
    if (FAILED(image_factory->GetImage(extent, flags, &hbitmap)) || hbitmap == nullptr)
        return {};

    std::string png;
    ComPtr<IWICImagingFactory> wic;
    if (SUCCEEDED(::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) {
        ComPtr<IWICBitmap> premultiplied;
        if (SUCCEEDED(wic->CreateBitmapFromHBITMAP(hbitmap, nullptr, WICBitmapUsePremultipliedAlpha, &premultiplied))) {
            // Model thumbnails come back on a transparent background; converting the
            // premultiplied DIB to straight alpha is what keeps it transparent in the PNG.
            ComPtr<IWICBitmapSource> straight;
            if (SUCCEEDED(::WICConvertBitmapSource(GUID_WICPixelFormat32bppBGRA, premultiplied, &straight)))
                png = encode_png(wic, straight);
        }
    }
    ::DeleteObject(hbitmap);
    return png;
}

}} // namespace Slic3r::GUI

#else // _WIN32

namespace Slic3r { namespace GUI {

std::string get_shell_thumbnail_png(const std::wstring & /* path */, int /* size */, bool /* cached_only */)
{
    return {};
}

}} // namespace Slic3r::GUI

#endif // _WIN32
