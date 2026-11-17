// Text capture: Windows.Media.Ocr on the captured image.
// Compiled as C++20 (C++/WinRT). Call from a worker thread.
#include "platform/Ocr.h"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Security.Cryptography.h>

#include <algorithm>

namespace bg {

using namespace winrt;
using namespace winrt::Windows::Graphics::Imaging;
using namespace winrt::Windows::Media::Ocr;

bool ocrImage(const Image& im, std::wstring& text, std::string& err) {
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    } catch (...) {
    }
    try {
        OcrEngine engine = OcrEngine::TryCreateFromLanguage(winrt::Windows::Globalization::Language(L"ko-KR"));
        if (!engine) engine = OcrEngine::TryCreateFromUserProfileLanguages();
        if (!engine) { err = "Windows 문자 인식(OCR) 언어 팩이 없어요"; return false; }
        // OCR engines limit the size; upscale small captures a little for better recognition
        int w = im.w, h = im.h;
        uint32_t maxDim = OcrEngine::MaxImageDimension();
        if ((uint32_t)std::max(w, h) > maxDim) { err = "이미지가 너무 커요"; return false; }
        SoftwareBitmap bmp(BitmapPixelFormat::Bgra8, w, h, BitmapAlphaMode::Premultiplied);
        auto buf = winrt::Windows::Security::Cryptography::CryptographicBuffer::CreateFromByteArray(
            winrt::array_view<const uint8_t>((const uint8_t*)im.px.data(), (const uint8_t*)im.px.data() + im.bytes()));
        bmp.CopyFromBuffer(buf);
        OcrResult r = engine.RecognizeAsync(bmp).get();
        std::wstring out;
        for (auto const& line : r.Lines()) {
            if (!out.empty()) out += L"\r\n";
            out += line.Text().c_str();
        }
        text = out;
        return true;
    } catch (winrt::hresult_error const& e) {
        std::wstring m = e.message().c_str();
        err = "문자 인식 실패";
        (void)m;
        return false;
    }
}

} // namespace bg
