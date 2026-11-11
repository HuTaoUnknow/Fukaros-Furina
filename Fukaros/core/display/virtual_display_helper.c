/* Elevated installation is kept separate from the UI and video process.
 * Never disable signature checks, import root certificates, or change other
 * display adapters. Package bytes are pinned to upstream release 25.7.23. */
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <setupapi.h>
#include <newdev.h>
#include <wincrypt.h>
#include <wintrust.h>
#include <softpub.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "helper_launch.h"
#include "virtual_display.h"
static const GUID display_class = {
    0x4d36e968, 0xe325, 0x11ce, {0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18}};

static int hash_matches(const wchar_t* file, const char* expected) {
    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    BYTE bytes[32768], digest[32];
    DWORD n, size = 32;
    char actual[65];
    HANDLE h;
    int ok = 0;
    h = CreateFileW(file, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    if (CryptAcquireContextW(&provider, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) &&
        CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash)) {
        ok = 1;
        while (ReadFile(h, bytes, sizeof(bytes), &n, NULL) && n)
            if (!CryptHashData(hash, bytes, n, 0)) {
                ok = 0;
                break;
            }
        if (!CryptGetHashParam(hash, HP_HASHVAL, digest, &size, 0))
            ok = 0;
        for (n = 0; n < 32; n++)
            sprintf(actual + n * 2, "%02X", digest[n]);
        actual[64] = 0;
        ok = ok && !strcmp(actual, expected);
    }
    if (hash)
        CryptDestroyHash(hash);
    if (provider)
        CryptReleaseContext(provider, 0);
    CloseHandle(h);
    return ok;
}

static int valid_package(const wchar_t* dir) {
    const wchar_t* names[] = {L"MttVDD.inf", L"MttVDD.dll", L"mttvdd.cat"};
    const char* hashes[] = {"550D211FE481E74DFE3F9D724ED78BE48B3A9113405965D683D9373E8D672F5D",
                            "C9CA837F57A98FBD43BC416A7F535A95843626E7759EAF85CF0CD7CE334DBB05",
                            "08A0093FC9B2E32B287A6F8A77CA4DE0A31830D29FC33D2B13A918DC859468F6"};
    wchar_t path[MAX_PATH];
    unsigned i;
    WINTRUST_FILE_INFO file = {0};
    WINTRUST_DATA trust = {0};
    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    LONG result;
    if (wcslen(dir) > MAX_PATH - 20)
        return 0;
    for (i = 0; i < 3; i++) {
        swprintf(path, MAX_PATH, L"%ls\\%ls", dir, names[i]);
        if (!hash_matches(path, hashes[i]))
            return 0;
    }
    file.cbStruct = sizeof(file);
    file.pcwszFilePath = path;
    trust.cbStruct = sizeof(trust);
    trust.dwUIChoice = WTD_UI_NONE;
    trust.fdwRevocationChecks = WTD_REVOKE_NONE;
    trust.dwUnionChoice = WTD_CHOICE_FILE;
    trust.pFile = &file;
    trust.dwStateAction = WTD_STATEACTION_VERIFY;
    trust.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;
    result = WinVerifyTrust(NULL, &action, &trust);
    trust.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(NULL, &action, &trust);
    return result == ERROR_SUCCESS;
}

static int settings_keep_existing(const char* xml) {
    const wchar_t* path = L"C:\\VirtualDisplayDriver\\vdd_settings.xml";
    HANDLE file;
    LARGE_INTEGER size;
    char* buf;
    DWORD read = 0;
    int incomplete;
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return 0;
    if (!GetFileSizeEx(file, &size)) {
        CloseHandle(file);
        return 1;
    }
    if (size.QuadPart > 1024 * 1024) {
        CloseHandle(file);
        return 1;
    }
    if (size.QuadPart <= 0) {
        CloseHandle(file);
        return 0;
    }
    buf = (char*)calloc(1, (size_t)size.QuadPart);
    if (!buf) {
        CloseHandle(file);
        return 1;
    }
    if (!ReadFile(file, buf, (DWORD)size.QuadPart, &read, NULL)) {
        free(buf);
        CloseHandle(file);
        return 1;
    }
    CloseHandle(file);
    incomplete = fr_helper_settings_incomplete(buf, read, xml, strlen(xml));
    free(buf);
    return !incomplete;
}

static int prepare_settings(void) {
    const char xml[] =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<vdd_settings><monitors><count>1</count></monitors><gpu><friendlyname>default</friendlyname></gpu><global><g_refresh_rate>60</g_refresh_rate></global><resolutions><resolution><width>1920</width><height>1200</height><refresh_rate>60</refresh_rate></resolution><resolution><width>1920</width><height>1080</height><refresh_rate>60</refresh_rate></resolution><resolution><width>1440</width><height>900</height><refresh_rate>60</refresh_rate></resolution></resolutions><options><CustomEdid>false</CustomEdid><PreventSpoof>false</PreventSpoof><EdidCeaOverride>false</EdidCeaOverride><HardwareCursor>true</HardwareCursor><SDR10bit>false</SDR10bit><HDRPlus>false</HDRPlus><logging>false</logging><debuglogging>false</debuglogging></options></vdd_settings>\n";
    HANDLE h;
    DWORD written;
    if (settings_keep_existing(xml))
        return 1;
    /* A crashed write leaves a short prefix of this template. Remove only that residue. */
    DeleteFileW(L"C:\\VirtualDisplayDriver\\vdd_settings.xml");
    if (!CreateDirectoryW(L"C:\\VirtualDisplayDriver", NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
        return 0;
    h = CreateFileW(L"C:\\VirtualDisplayDriver\\vdd_settings.xml", GENERIC_WRITE, 0, NULL, CREATE_NEW,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    if (!WriteFile(h, xml, sizeof(xml) - 1, &written, NULL) || written != sizeof(xml) - 1) {
        CloseHandle(h);
        return 0;
    }
    CloseHandle(h);
    return 1;
}

static int own_device(HDEVINFO list, SP_DEVINFO_DATA* dev) {
    DWORD i;
    wchar_t ids[1024], label[1024];
    for (i = 0;; i++) {
        memset(dev, 0, sizeof(*dev));
        dev->cbSize = sizeof(*dev);
        if (!SetupDiEnumDeviceInfo(list, i, dev))
            return 0;
        if (SetupDiGetDeviceRegistryPropertyW(list, dev, SPDRP_HARDWAREID, NULL, (BYTE*)ids, sizeof(ids),
                                              NULL) &&
            !_wcsicmp(ids, L"Root\\MttVDD") &&
            SetupDiGetDeviceRegistryPropertyW(list, dev, SPDRP_FRIENDLYNAME, NULL, (BYTE*)label,
                                              sizeof(label), NULL) &&
            !wcscmp(label, L"Furina Virtual Display"))
            return 1;
    }
}

static int detach_installed(void) {
    return fr_virtual_available() && fr_virtual_detach_idle() == 0;
}

static int uninstall_device(const wchar_t* package) {
    HDEVINFO list;
    SP_DEVINFO_DATA dev = {0};
    DWORD error;
    (void)package;
    list = SetupDiGetClassDevsW(&display_class, NULL, NULL, DIGCF_PRESENT);
    if (list == INVALID_HANDLE_VALUE)
        return 12;
    if (!own_device(list, &dev)) {
        SetupDiDestroyDeviceInfoList(list);
        return 0;
    }
    detach_installed();
    if (!SetupDiCallClassInstaller(DIF_REMOVE, list, &dev)) {
        error = GetLastError();
        SetupDiDestroyDeviceInfoList(list);
        return (int)error;
    }
    SetupDiDestroyDeviceInfoList(list);
    return 0;
}

static int install_package(const wchar_t* package) {
    HDEVINFO list;
    SP_DEVINFO_DATA dev = {0};
    wchar_t inf[MAX_PATH];
    const wchar_t ids[] = L"Root\\MttVDD\0", label[] = L"Furina Virtual Display";
    BOOL reboot = FALSE;
    int created = 0;
    DWORD error, i;
    if (!package || !valid_package(package))
        return 11;
    if (!prepare_settings())
        return 13;
    list = SetupDiGetClassDevsW(&display_class, NULL, NULL, DIGCF_PRESENT);
    if (list == INVALID_HANDLE_VALUE)
        return 12;
    if (!own_device(list, &dev)) {
        dev.cbSize = sizeof(dev);
        if (!SetupDiCreateDeviceInfoW(list, L"Display", &display_class, L"Furina Virtual Display", NULL,
                                      DICD_GENERATE_ID, &dev) ||
            !SetupDiSetDeviceRegistryPropertyW(list, &dev, SPDRP_HARDWAREID, (BYTE*)ids, sizeof(ids)) ||
            !SetupDiSetDeviceRegistryPropertyW(list, &dev, SPDRP_FRIENDLYNAME, (BYTE*)label, sizeof(label)) ||
            !SetupDiCallClassInstaller(DIF_REGISTERDEVICE, list, &dev)) {
            error = GetLastError();
            SetupDiDestroyDeviceInfoList(list);
            return (int)error;
        }
        created = 1;
    }
    swprintf(inf, MAX_PATH, L"%ls\\MttVDD.inf", package);
    if (!UpdateDriverForPlugAndPlayDevicesW(NULL, L"Root\\MttVDD", inf, INSTALLFLAG_FORCE, &reboot)) {
        error = GetLastError();
        if (created)
            SetupDiCallClassInstaller(DIF_REMOVE, list, &dev);
        SetupDiDestroyDeviceInfoList(list);
        return (int)error;
    }
    SetupDiSetDeviceRegistryPropertyW(list, &dev, SPDRP_FRIENDLYNAME, (BYTE*)label, sizeof(label));
    SetupDiDestroyDeviceInfoList(list);
    if (reboot)
        return 3;
    /* Leave newly installed monitors inactive. The application activates only
     * after connection, and cancellation during installation cannot strand one. */
    for (i = 0; i < 150; i++) {
        if (detach_installed())
            return 0;
        Sleep(100);
    }
    return 14;
}

static int with_guard(int (*command)(const wchar_t*), const wchar_t* package) {
    HANDLE mutex;
    DWORD wait;
    int code;
    /* Drop orphaned helpers, truncated temp files, and a display left active by a crash. */
    fr_helper_reap_protected_helper();
    fr_virtual_detach_idle();
    mutex = CreateMutexW(NULL, FALSE, L"Local\\FukarosVirtualDisplayHelper");
    if (!mutex)
        return 12;
    wait = WaitForSingleObject(mutex, 0);
    if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) {
        CloseHandle(mutex);
        return 16;
    }
    fr_helper_note_own_pid();
    code = command(package);
    fr_helper_clear_own_pid();
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return code;
}

static void refuse_writable_image(void) {
    fwprintf(stderr, L"Fukaros: refusing to run virtual_display_helper from a user-writable directory.\n");
}

int wmain(int argc, wchar_t** argv) {
    SetProcessDPIAware();
    if (argc < 2)
        return 10;
    if (!wcscmp(argv[1], L"verify")) {
        if (argc != 3)
            return 10;
        return valid_package(argv[2]) ? 0 : 11;
    }
    if (!wcscmp(argv[1], L"uninstall")) {
        if (fr_helper_image_user_writable()) {
            refuse_writable_image();
            return 15;
        }
        return with_guard(uninstall_device, L"");
    }
    if (wcscmp(argv[1], L"install") || argc != 3)
        return 10;
    if (fr_helper_image_user_writable()) {
        refuse_writable_image();
        return 15;
    }
    return with_guard(install_package, argv[2]);
}
