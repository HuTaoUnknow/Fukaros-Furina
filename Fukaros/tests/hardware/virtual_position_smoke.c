/* Manual hardware check. Moves only the temporary tablet extension. */
#include "virtual_display.h"
#include <stdio.h>
#include <string.h>

typedef struct physical_screen {
    wchar_t name[32];
    DEVMODEW mode;
} physical_screen;

static int preserved(physical_screen* screens, unsigned count) {
    for (unsigned i = 0; i < count; i++) {
        DEVMODEW mode = {0};
        mode.dmSize = sizeof(mode);
        if (!EnumDisplaySettingsW(screens[i].name, ENUM_CURRENT_SETTINGS, &mode))
            return 0;
        DEVMODEW* old = &screens[i].mode;
        if (mode.dmPelsWidth != old->dmPelsWidth || mode.dmPelsHeight != old->dmPelsHeight ||
            mode.dmDisplayFrequency != old->dmDisplayFrequency || mode.dmPosition.x != old->dmPosition.x ||
            mode.dmPosition.y != old->dmPosition.y)
            return 0;
    }
    return 1;
}

int main(void) {
    physical_screen screens[32];
    unsigned count = 0;
    DISPLAY_DEVICEW device;
    long x = 0, y = 0;
    int valid = 1;
    if (!fr_virtual_available())
        return 77;
    for (DWORD i = 0;; i++) {
        memset(&device, 0, sizeof(device));
        device.cb = sizeof(device);
        if (!EnumDisplayDevicesW(NULL, i, &device, 0))
            break;
        if ((device.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) &&
            _wcsicmp(device.DeviceID, L"Root\\MttVDD") && count < 32) {
            wcscpy(screens[count].name, device.DeviceName);
            memset(&screens[count].mode, 0, sizeof(DEVMODEW));
            screens[count].mode.dmSize = sizeof(DEVMODEW);
            if (EnumDisplaySettingsW(device.DeviceName, ENUM_CURRENT_SETTINGS, &screens[count].mode))
                count++;
        }
    }
    const POINT positions[] = {{-1920, 0}, {-1200, 0}, {0, -1920}, {0, -1200}};
    for (unsigned cycle = 0; cycle < 4; cycle++) {
        int portrait = cycle == 1 || cycle == 2;
        fr_virtual_display display = {0};
        long result = fr_virtual_acquire_oriented(&display, portrait);
        if (result) {
            printf("acquire error=%ld\n", result);
            return 1;
        }
        char name[128] = {0};
        DEVMODEA mode = {0};
        mode.dmSize = sizeof(mode);
        int matching = fr_virtual_name(&display, name, sizeof(name)) &&
                       EnumDisplaySettingsA(name, ENUM_CURRENT_SETTINGS, &mode) &&
                       mode.dmPelsWidth == (portrait ? 1200u : 1920u) &&
                       mode.dmPelsHeight == (portrait ? 1920u : 1200u) && mode.dmDisplayFrequency == 60;
        printf("portrait=%d desktop=%lux%lu refresh=%lu matching=%d\n", portrait, mode.dmPelsWidth,
               mode.dmPelsHeight, mode.dmDisplayFrequency, matching);
        if (!matching)
            valid = 0;
        result = fr_virtual_set_position(&display, positions[cycle].x, positions[cycle].y);
        if (result || fr_virtual_get_position(&display, &x, &y) || x != positions[cycle].x ||
            y != positions[cycle].y || !preserved(screens, count))
            valid = 0;
        printf("cycle=%u set=%ld position=%ld,%ld physical_preserved=%d\n", cycle, result, x, y,
               preserved(screens, count));
        fflush(stdout);
        if (fr_virtual_release(&display) || !preserved(screens, count))
            valid = 0;
    }
    printf("restored=%d passed=%d\n", preserved(screens, count), valid);
    return valid ? 0 : 2;
}
