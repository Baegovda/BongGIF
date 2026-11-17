// BongGIF 2.0 - screen-to-GIF recorder/editor (native Win32, C++17).
#include "platform/Capture.h"
#include "platform/Media.h"
#include "ui/App.h"

#include <shellapi.h>
#include <windows.h>

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    bg::setDpiAware();
    bg::mediaStartup();
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring open = argc > 1 ? argv[1] : L"";
    LocalFree(argv);
    int rc = 0;
    {
        bg::App app;
        if (!app.create(inst, open)) return 1;
        MSG m;
        while (GetMessageW(&m, nullptr, 0, 0) > 0) {
            if (app.preTranslate(m)) continue;
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        rc = (int)m.wParam;
    }
    bg::mediaShutdown();
    return rc;
}
