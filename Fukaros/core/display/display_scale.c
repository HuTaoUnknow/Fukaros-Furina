#include "display_scale.h"
#include "screen_scale.h"
#ifdef _WIN32
#include <stdlib.h>
#include <wchar.h>

/* Source-relative DPI packets are not declared by the Windows SDK. Keep this
 * compatibility boundary private, validate all returned slots, and propagate
 * unsupported results. Never use global DPI settings or registry writes.
 * Packet reference: github.com/lihas/windows-DPI-scaling-sample (DPIHelper.h). */
typedef struct dpi_query {
    DISPLAYCONFIG_DEVICE_INFO_HEADER header;
    INT32 minimum, current, maximum;
} dpi_query;

typedef struct dpi_set {
    DISPLAYCONFIG_DEVICE_INFO_HEADER header;
    INT32 relative;
} dpi_set;

_Static_assert(sizeof(dpi_query) == 32, "Unexpected source DPI query layout");
_Static_assert(sizeof(dpi_set) == 24, "Unexpected source DPI setter layout");

static int same_source(const DISPLAYCONFIG_PATH_INFO* a, const DISPLAYCONFIG_PATH_INFO* b) {
    return a->sourceInfo.id == b->sourceInfo.id &&
           a->sourceInfo.adapterId.HighPart == b->sourceInfo.adapterId.HighPart &&
           a->sourceInfo.adapterId.LowPart == b->sourceInfo.adapterId.LowPart;
}

static LONG query_virtual(const fr_virtual_display* display, dpi_query* dpi) {
    char name[128];
    wchar_t wide[128];
    LONG result = ERROR_NOT_FOUND;
    if (!display || !display->acquired || !dpi)
        return ERROR_INVALID_PARAMETER;
    if (!fr_virtual_name(display, name, sizeof(name)) ||
        !MultiByteToWideChar(CP_UTF8, 0, name, -1, wide, 128))
        return ERROR_NOT_FOUND;
    for (int retry = 0; retry < 4; retry++) {
        UINT32 count = 0, nmodes = 0;
        DISPLAYCONFIG_PATH_INFO* paths = NULL;
        DISPLAYCONFIG_MODE_INFO* modes = NULL;
        result = GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &count, &nmodes);
        if (result)
            return result;
        paths = calloc(count + 1, sizeof(*paths));
        modes = calloc(nmodes + 1, sizeof(*modes));
        if (!paths || !modes) {
            free(paths);
            free(modes);
            return ERROR_OUTOFMEMORY;
        }
        result = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &count, paths, &nmodes, modes, NULL);
        if (!result) {
            result = ERROR_NOT_FOUND;
            for (UINT32 i = 0; i < count; i++) {
                DISPLAYCONFIG_SOURCE_DEVICE_NAME source = {0};
                source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
                source.header.size = sizeof(source);
                source.header.adapterId = paths[i].sourceInfo.adapterId;
                source.header.id = paths[i].sourceInfo.id;
                if (DisplayConfigGetDeviceInfo(&source.header) || _wcsicmp(source.viewGdiDeviceName, wide))
                    continue;
                // A shared source would also change a physical display's DPI.
                for (UINT32 j = 0; j < count; j++)
                    if (i != j && same_source(paths + i, paths + j)) {
                        free(paths);
                        free(modes);
                        return ERROR_BAD_CONFIGURATION;
                    }
                dpi->header = source.header;
                dpi->header.type = (DISPLAYCONFIG_DEVICE_INFO_TYPE)-3;
                dpi->header.size = sizeof(*dpi);
                result = DisplayConfigGetDeviceInfo(&dpi->header);
                if (!result &&
                    (!fr_screen_scale_absolute(dpi->minimum, dpi->current) || dpi->current > dpi->maximum ||
                     dpi->maximum < 0 || dpi->maximum > 11 || dpi->maximum - dpi->minimum > 11))
                    result = ERROR_INVALID_DATA;
                break;
            }
        }
        free(paths);
        free(modes);
        if (result != ERROR_INSUFFICIENT_BUFFER)
            return result;
    }
    return result;
}

long fr_virtual_get_scale(const fr_virtual_display* display, int* percent) {
    dpi_query dpi = {0};
    LONG result;
    if (!percent)
        return ERROR_INVALID_PARAMETER;
    result = query_virtual(display, &dpi);
    if (!result)
        *percent = fr_screen_scale_absolute(dpi.minimum, dpi.current);
    return result;
}

long fr_virtual_set_scale(const fr_virtual_display* display, int percent, int* applied) {
    dpi_query dpi = {0};
    dpi_set request = {0};
    LONG result;
    int relative;
    if (applied)
        *applied = 0;
    if (!fr_screen_scale_valid(percent))
        return ERROR_INVALID_PARAMETER;
    result = query_virtual(display, &dpi);
    if (result)
        return result;
    if (applied)
        *applied = fr_screen_scale_absolute(dpi.minimum, dpi.current);
    if (!fr_screen_scale_relative(percent, dpi.minimum, dpi.maximum, &relative))
        return ERROR_NOT_SUPPORTED;
    if (relative != dpi.current) {
        request.header = dpi.header;
        request.header.type = (DISPLAYCONFIG_DEVICE_INFO_TYPE)-4;
        request.header.size = sizeof(request);
        request.relative = relative;
        result = DisplayConfigSetDeviceInfo(&request.header);
        if (result)
            return result;
    }
    if (applied)
        *applied = percent;
    return ERROR_SUCCESS;
}
#else
long fr_virtual_get_scale(const fr_virtual_display* d, int* p) {
    (void)d;
    (void)p;
    return 50;
}

long fr_virtual_set_scale(const fr_virtual_display* d, int p, int* a) {
    (void)d;
    (void)p;
    if (a)
        *a = 0;
    return 50;
}
#endif
