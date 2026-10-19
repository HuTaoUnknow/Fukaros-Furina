/* Explicit real-desktop smoke test; intentionally not in automatic CTest. */
#include "virtual_display.h"
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
typedef struct saved_display {
    wchar_t name[32];
    DEVMODEW mode;
} saved_display;

static int unchanged(saved_display* saved, unsigned n) {
    for (unsigned i = 0; i < n; i++) {
        DEVMODEW m = {0};
        m.dmSize = sizeof(m);
        if (!EnumDisplaySettingsW(saved[i].name, ENUM_CURRENT_SETTINGS, &m))
            return 0;
        DEVMODEW* b = &saved[i].mode;
        if (m.dmPelsWidth != b->dmPelsWidth || m.dmPelsHeight != b->dmPelsHeight ||
            m.dmDisplayFrequency != b->dmDisplayFrequency || m.dmPosition.x != b->dmPosition.x ||
            m.dmPosition.y != b->dmPosition.y)
            return 0;
    }
    return 1;
}

static int detached(void) {
    DISPLAY_DEVICEW d;
    for (DWORD i = 0;; i++) {
        memset(&d, 0, sizeof(d));
        d.cb = sizeof(d);
        if (!EnumDisplayDevicesW(NULL, i, &d, 0))
            break;
        if (!_wcsicmp(d.DeviceID, L"Root\\MttVDD") && (d.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP))
            return 0;
    }
    return 1;
}

int main(void) {
    saved_display saved[32];
    unsigned count = 0;
    DISPLAY_DEVICEW device;
    if (!fr_virtual_available()) {
        puts("Virtual driver unavailable");
        return 77;
    }
    for (DWORD i = 0;; i++) {
        memset(&device, 0, sizeof(device));
        device.cb = sizeof(device);
        if (!EnumDisplayDevicesW(NULL, i, &device, 0))
            break;
        if ((device.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) &&
            _wcsicmp(device.DeviceID, L"Root\\MttVDD") && count < 32) {
            wcscpy(saved[count].name, device.DeviceName);
            memset(&saved[count].mode, 0, sizeof(DEVMODEW));
            saved[count].mode.dmSize = sizeof(DEVMODEW);
            if (EnumDisplaySettingsW(device.DeviceName, ENUM_CURRENT_SETTINGS, &saved[count].mode))
                count++;
        }
    }
    for (unsigned cycle = 0; cycle < 2; cycle++) {
        /* Reproduce a target already active before our session, including left placement. */
        fr_virtual_display leftover = {0};
        if (cycle == 1) {
            LONG e = fr_virtual_acquire(&leftover);
            if (e)
                return 3;
            if (fr_virtual_set_position(&leftover, -1920, 0)) {
                fr_virtual_release(&leftover);
                return 4;
            }
        }
        fr_virtual_display d = {0};
        LONG e = fr_virtual_acquire(&d);
        if (e) {
            printf("acquire error %ld\n", e);
            return 1;
        }
        if (cycle == 1 && fr_virtual_set_position(&d, -1920, 0)) {
            fr_virtual_release(&d);
            fr_virtual_release(&leftover);
            return 5;
        }
        DEVMODEW mode = {0};
        mode.dmSize = sizeof(mode);
        int correct = EnumDisplaySettingsW(d.name, ENUM_CURRENT_SETTINGS, &mode) &&
                      mode.dmPelsWidth == 1920 && mode.dmPelsHeight == 1200 &&
                      mode.dmDisplayFrequency == 60 && unchanged(saved, count);
        Sleep(200);
        e = fr_virtual_release(&d);
        int inactive = detached();
        fr_virtual_release(&leftover);
        if (!correct || e || !inactive || !unchanged(saved, count)) {
            printf("mode/detach failure: correct=%d inactive=%d error=%ld\n", correct, inactive, e);
            return 2;
        }
        printf("cycle %u: 1920x1200 / 60 Hz; physical displays preserved; virtual detached; preattached=%u\n",
               cycle + 1, cycle == 1);
    }
    return 0;
}
#else
int main(void) {
    return 77;
}
#endif
