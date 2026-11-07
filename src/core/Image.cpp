#include "Image.h"
#include "Log.h"
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

namespace aether {

using Microsoft::WRL::ComPtr;

bool savePngBGRA(const std::wstring& path, uint32_t w, uint32_t h, uint32_t pitch, const uint8_t* pixels) {
    HRESULT hrCo = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool ok = false;
    {
        ComPtr<IWICImagingFactory> f;
        ComPtr<IWICStream> stream;
        ComPtr<IWICBitmapEncoder> enc;
        ComPtr<IWICBitmapFrameEncode> frame;
        if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(f.GetAddressOf()))) &&
            SUCCEEDED(f->CreateStream(stream.GetAddressOf())) &&
            SUCCEEDED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) &&
            SUCCEEDED(f->CreateEncoder(GUID_ContainerFormatPng, nullptr, enc.GetAddressOf())) &&
            SUCCEEDED(enc->Initialize(stream.Get(), WICBitmapEncoderNoCache)) &&
            SUCCEEDED(enc->CreateNewFrame(frame.GetAddressOf(), nullptr)) && SUCCEEDED(frame->Initialize(nullptr)) &&
            SUCCEEDED(frame->SetSize(w, h))) {
            WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
            if (SUCCEEDED(frame->SetPixelFormat(&fmt)) &&
                SUCCEEDED(frame->WritePixels(h, pitch, pitch * h, const_cast<BYTE*>(pixels))) &&
                SUCCEEDED(frame->Commit()) && SUCCEEDED(enc->Commit()))
                ok = true;
        }
    }
    if (SUCCEEDED(hrCo)) CoUninitialize();
    if (!ok) LOGE("PNG save failed");
    return ok;
}

} // namespace aether
