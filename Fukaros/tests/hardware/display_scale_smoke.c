/* Deliberate real-display test. Skips if a virtual target is already in use. */
#include "display_scale.h"
#include <stdio.h>
#ifdef _WIN32
#include <wchar.h>

static int attached(void) {
    DISPLAY_DEVICEW d;
    for (DWORD i = 0;; i++) {
        ZeroMemory(&d, sizeof(d));
        d.cb = sizeof(d);
        if (!EnumDisplayDevicesW(NULL, i, &d, 0))
            break;
        if (!_wcsicmp(d.DeviceID, L"Root\\MttVDD") && (d.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP))
            return 1;
    }
    return 0;
}

static UINT window_dpi(LONG x, LONG y) {
    HWND window = CreateWindowExW(WS_EX_TOOLWINDOW, L"FurinaScaleProbe", L"", WS_POPUP, x + 24, y + 24, 180,
                                  120, NULL, NULL, GetModuleHandleW(NULL), NULL);
    UINT dpi = window ? GetDpiForWindow(window) : 0;
    if (window)
        DestroyWindow(window);
    return dpi;
}

int main(void) {
    if (attached() || !fr_virtual_available()) {
        puts("SKIP: virtual target active or driver unavailable");
        return 77;
    }
    SetProcessDPIAware();
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSW cls = {0};
    cls.lpfnWndProc = DefWindowProcW;
    cls.hInstance = GetModuleHandleW(NULL);
    cls.lpszClassName = L"FurinaScaleProbe";
    if (!RegisterClassW(&cls))
        return 5;
    UINT primaryDpi = window_dpi(0, 0);
    DEVMODEW primary = {0};
    primary.dmSize = sizeof(primary);
    EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &primary);
    int result = 0;
    for (int portrait = 0; portrait < 2; portrait++) {
        fr_virtual_display display = {0};
        LONG e = fr_virtual_acquire_oriented(&display, portrait);
        if (e) {
            printf("acquire error %ld\n", e);
            return 1;
        }
        Sleep(400);
        int original = 0;
        fr_virtual_get_scale(&display, &original);
        const int scales[] = {150, 100};
        for (unsigned i = 0; i < 2; i++) {
            int actual = 0;
            e = fr_virtual_set_scale(&display, scales[i], &actual);
            Sleep(350);
            int observed = 0;
            LONG read = fr_virtual_get_scale(&display, &observed);
            DEVMODEW mode = {0};
            mode.dmSize = sizeof(mode);
            EnumDisplaySettingsW(display.name, ENUM_CURRENT_SETTINGS, &mode);
            DEVMODEW now = {0};
            now.dmSize = sizeof(now);
            EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &now);
            int same = now.dmPelsWidth == primary.dmPelsWidth && now.dmPelsHeight == primary.dmPelsHeight &&
                       now.dmPosition.x == primary.dmPosition.x && now.dmPosition.y == primary.dmPosition.y;
            UINT virtualDpi = window_dpi(mode.dmPosition.x, mode.dmPosition.y);
            int primaryDpiSame = primaryDpi && primaryDpi == window_dpi(0, 0);
            printf("effective_dpi=%u primary_dpi_unchanged=%d\n", virtualDpi, primaryDpiSame);
            if (virtualDpi != (UINT)(scales[i] * 96 / 100) || !primaryDpiSame)
                result = 2;
            printf(
                "portrait=%d requested=%d applied=%d observed=%d error=%ld read=%ld resolution=%lux%lu primary_unchanged=%d\n",
                portrait, scales[i], actual, observed, e, read, mode.dmPelsWidth, mode.dmPelsHeight, same);
            if (e || read || observed != scales[i] || !same || mode.dmPelsWidth != (portrait ? 1200 : 1920) ||
                mode.dmPelsHeight != (portrait ? 1920 : 1200))
                result = 2;
        }
        if (original) {
            int actual;
            fr_virtual_set_scale(&display, original, &actual);
        }
        if (fr_virtual_release(&display))
            result = 3;
        Sleep(250);
        if (attached())
            result = 4;
    }
    return result;
}
#else
int main(void) {
    return 77;
}
#endif
