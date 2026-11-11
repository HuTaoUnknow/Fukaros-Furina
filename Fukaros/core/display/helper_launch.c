#include "helper_launch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <aclapi.h>
#include <tlhelp32.h>
#endif

static const unsigned fr_sha256_k[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

static unsigned rotr(unsigned value, unsigned bits) {
    return (value >> bits) | (value << (32 - bits));
}

static void sha256_block(fr_helper_sha256_ctx* ctx, const unsigned char block[64]) {
    unsigned w[64];
    unsigned a, b, c, d, e, f, g, h;
    unsigned i;
    for (i = 0; i < 16; i++) {
        w[i] = ((unsigned)block[i * 4] << 24) | ((unsigned)block[i * 4 + 1] << 16) |
               ((unsigned)block[i * 4 + 2] << 8) | (unsigned)block[i * 4 + 3];
    }
    for (i = 16; i < 64; i++) {
        unsigned s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        unsigned s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];
    for (i = 0; i < 64; i++) {
        unsigned s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        unsigned ch = (e & f) ^ (~e & g);
        unsigned temp1 = h + s1 + ch + fr_sha256_k[i] + w[i];
        unsigned s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        unsigned maj = (a & b) ^ (a & c) ^ (b & c);
        unsigned temp2 = s0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }
    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

void fr_helper_sha256_init(fr_helper_sha256_ctx* ctx) {
    ctx->state[0] = 0x6a09e667u;
    ctx->state[1] = 0xbb67ae85u;
    ctx->state[2] = 0x3c6ef372u;
    ctx->state[3] = 0xa54ff53au;
    ctx->state[4] = 0x510e527fu;
    ctx->state[5] = 0x9b05688cu;
    ctx->state[6] = 0x1f83d9abu;
    ctx->state[7] = 0x5be0cd19u;
    ctx->bitlen = 0;
    ctx->buflen = 0;
}

void fr_helper_sha256_update(fr_helper_sha256_ctx* ctx, const void* data, size_t length) {
    const unsigned char* bytes = (const unsigned char*)data;
    ctx->bitlen += (unsigned long long)length * 8u;
    while (length) {
        unsigned space = 64u - ctx->buflen;
        unsigned n = (unsigned)length;
        if (n > space)
            n = space;
        memcpy(ctx->buffer + ctx->buflen, bytes, n);
        ctx->buflen += n;
        bytes += n;
        length -= n;
        if (ctx->buflen == 64u) {
            sha256_block(ctx, ctx->buffer);
            ctx->buflen = 0;
        }
    }
}

static void put_hex(char* out, unsigned value) {
    static const char digits[] = "0123456789ABCDEF";
    int i;
    for (i = 7; i >= 0; i--) {
        out[i] = digits[value & 0xfu];
        value >>= 4;
    }
}

void fr_helper_sha256_final(fr_helper_sha256_ctx* ctx, char out[65]) {
    unsigned long long bits = ctx->bitlen;
    unsigned i;
    ctx->buffer[ctx->buflen++] = 0x80u;
    if (ctx->buflen > 56u) {
        while (ctx->buflen < 64u)
            ctx->buffer[ctx->buflen++] = 0;
        sha256_block(ctx, ctx->buffer);
        ctx->buflen = 0;
    }
    while (ctx->buflen < 56u)
        ctx->buffer[ctx->buflen++] = 0;
    for (i = 0; i < 8u; i++)
        ctx->buffer[56u + i] = (unsigned char)((bits >> ((7u - i) * 8u)) & 0xffu);
    sha256_block(ctx, ctx->buffer);
    for (i = 0; i < 8u; i++)
        put_hex(out + i * 8u, ctx->state[i]);
    out[64] = 0;
}

void fr_helper_sha256(const void* data, size_t length, char out[65]) {
    fr_helper_sha256_ctx ctx;
    fr_helper_sha256_init(&ctx);
    fr_helper_sha256_update(&ctx, data, length);
    fr_helper_sha256_final(&ctx, out);
}

int fr_helper_launch_decide(int source_user_writable, int hash_matches, int staged_present,
                            int staged_hash_matches, int staged_dir_user_writable) {
    if (staged_present && staged_hash_matches && !staged_dir_user_writable)
        return FR_HELPER_LAUNCH_STAGED;
    if (!hash_matches)
        return FR_HELPER_LAUNCH_REJECT_HASH;
    if (!source_user_writable)
        return FR_HELPER_LAUNCH_IN_PLACE;
    return FR_HELPER_LAUNCH_NEED_STAGE;
}

int fr_helper_residue_stale(int pid_recorded, int process_alive, int parent_alive) {
    if (!pid_recorded)
        return 0;
    if (!process_alive || !parent_alive)
        return 1;
    return 0;
}

static int contains_marker(const char* text, size_t length) {
    const char marker[] = "</vdd_settings>";
    const size_t n = sizeof marker - 1;
    size_t i;
    if (!text || length < n)
        return 0;
    for (i = 0; i + n <= length; i++) {
        if (!memcmp(text + i, marker, n))
            return 1;
    }
    return 0;
}

int fr_helper_settings_incomplete(const char* text, size_t length, const char* templ,
                                  size_t template_length) {
    if (!text || length == 0)
        return 1;
    if (contains_marker(text, length))
        return 0;
    if (!templ || template_length == 0 || length >= template_length)
        return 0;
    return memcmp(text, templ, length) == 0;
}

#ifdef _WIN32
#define FR_WRITE_BITS                                                                                        \
    (FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES | FILE_DELETE_CHILD |       \
     DELETE | WRITE_DAC | WRITE_OWNER)

static HANDLE check_token(void) {
    HANDLE process = NULL, linked = NULL;
    TOKEN_ELEVATION_TYPE type = TokenElevationTypeDefault;
    DWORD size = 0;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &process))
        return NULL;
    if (GetTokenInformation(process, TokenElevationType, &type, sizeof type, &size) &&
        type == TokenElevationTypeFull &&
        GetTokenInformation(process, TokenLinkedToken, &linked, sizeof linked, &size) && linked) {
        CloseHandle(process);
        return linked;
    }
    return process;
}

static int sid_active(HANDLE token, PSID sid) {
    DWORD bytes = 0, i;
    TOKEN_USER* user = NULL;
    TOKEN_GROUPS* groups = NULL;
    int active = 0;
    if (!token || !sid || !IsValidSid(sid))
        return 0;
    GetTokenInformation(token, TokenUser, NULL, 0, &bytes);
    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && bytes >= sizeof(TOKEN_USER)) {
        user = (TOKEN_USER*)malloc(bytes);
        if (user && GetTokenInformation(token, TokenUser, user, bytes, &bytes) && EqualSid(user->User.Sid, sid))
            active = 1;
    }
    free(user);
    if (active)
        return 1;
    bytes = 0;
    GetTokenInformation(token, TokenGroups, NULL, 0, &bytes);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytes < sizeof(TOKEN_GROUPS))
        return 0;
    groups = (TOKEN_GROUPS*)malloc(bytes);
    if (!groups || !GetTokenInformation(token, TokenGroups, groups, bytes, &bytes)) {
        free(groups);
        return 0;
    }
    for (i = 0; i < groups->GroupCount; i++) {
        DWORD attr = groups->Groups[i].Attributes;
        if ((attr & SE_GROUP_ENABLED) && !(attr & SE_GROUP_USE_FOR_DENY_ONLY) &&
            EqualSid(groups->Groups[i].Sid, sid)) {
            active = 1;
            break;
        }
    }
    free(groups);
    return active;
}

/* Writable when any enabled SID on the unelevated token has an allow ACE with a write bit.
 * Deny ACEs are ignored, which can only classify a protected path as writable. */
static int path_user_writable(const wchar_t* path) {
    DWORD needed = 0, i;
    PSECURITY_DESCRIPTOR sd = NULL;
    BOOL present = FALSE, defaulted = FALSE;
    PACL dacl = NULL;
    HANDLE token;
    int writable = 1;
    if (!path || !path[0])
        return 1;
    token = check_token();
    if (!token)
        return 1;
    SetLastError(0);
    GetFileSecurityW(path, OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                     NULL, 0, &needed);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || needed < sizeof(SECURITY_DESCRIPTOR)) {
        CloseHandle(token);
        return 1;
    }
    sd = malloc(needed);
    if (!sd ||
        !GetFileSecurityW(path, OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                          sd, needed, &needed) ||
        !GetSecurityDescriptorDacl(sd, &present, &dacl, &defaulted) || !present || !dacl) {
        free(sd);
        CloseHandle(token);
        return 1;
    }
    writable = 0;
    for (i = 0; i < dacl->AceCount; i++) {
        ACCESS_ALLOWED_ACE* ace = NULL;
        if (!GetAce(dacl, i, (LPVOID*)&ace) || !ace)
            continue;
        if (ace->Header.AceFlags & INHERIT_ONLY_ACE)
            continue;
        if (ace->Header.AceType == ACCESS_ALLOWED_CALLBACK_ACE_TYPE ||
            ace->Header.AceType == ACCESS_DENIED_CALLBACK_ACE_TYPE ||
            ace->Header.AceType == ACCESS_ALLOWED_OBJECT_ACE_TYPE) {
            writable = 1;
            break;
        }
        if (ace->Header.AceType != ACCESS_ALLOWED_ACE_TYPE)
            continue;
        if ((ace->Mask & FR_WRITE_BITS) && sid_active(token, (PSID)&ace->SidStart)) {
            writable = 1;
            break;
        }
    }
    free(sd);
    CloseHandle(token);
    return writable;
}

int fr_helper_directory_user_writable(const wchar_t* path) {
    return path_user_writable(path);
}

int fr_helper_object_user_writable(const wchar_t* path) {
    return path_user_writable(path);
}

int fr_helper_image_user_writable(void) {
    wchar_t path[32768];
    DWORD n = GetModuleFileNameW(NULL, path, 32768);
    wchar_t* slash;
    if (!n || n >= 32768)
        return 1;
    if (fr_helper_object_user_writable(path))
        return 1;
    slash = wcsrchr(path, L'\\');
    if (!slash)
        return 1;
    *slash = 0;
    return fr_helper_directory_user_writable(path);
}

int fr_helper_file_sha256(const wchar_t* path, char out[65]) {
    HANDLE file;
    fr_helper_sha256_ctx ctx;
    unsigned char buf[65536];
    DWORD n = 0;
    if (!path || !out)
        return 0;
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return 0;
    fr_helper_sha256_init(&ctx);
    for (;;) {
        if (!ReadFile(file, buf, sizeof buf, &n, NULL)) {
            CloseHandle(file);
            return 0;
        }
        if (!n)
            break;
        fr_helper_sha256_update(&ctx, buf, n);
    }
    CloseHandle(file);
    fr_helper_sha256_final(&ctx, out);
    return 1;
}

static int protect_object(wchar_t* path, DWORD inheritance) {
    SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
    PSID system = NULL, admins = NULL, users = NULL;
    EXPLICIT_ACCESSW entries[3];
    PACL acl = NULL;
    DWORD result;
    int code = 12;
    if (!path)
        return 12;
    if (!AllocateAndInitializeSid(&nt, 1, SECURITY_LOCAL_SYSTEM_RID, 0, 0, 0, 0, 0, 0, 0, &system) ||
        !AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0,
                                  &admins) ||
        !AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_USERS, 0, 0, 0, 0, 0, 0,
                                  &users))
        goto done;
    memset(entries, 0, sizeof entries);
    entries[0].grfAccessPermissions = FILE_ALL_ACCESS;
    entries[0].grfAccessMode = SET_ACCESS;
    entries[0].grfInheritance = inheritance;
    entries[0].Trustee.TrusteeForm = TRUSTEE_IS_SID;
    entries[0].Trustee.TrusteeType = TRUSTEE_IS_USER;
    entries[0].Trustee.ptstrName = (LPWSTR)system;
    entries[1] = entries[0];
    entries[1].Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
    entries[1].Trustee.ptstrName = (LPWSTR)admins;
    entries[2] = entries[0];
    entries[2].grfAccessPermissions = FILE_GENERIC_READ | FILE_GENERIC_EXECUTE;
    entries[2].Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
    entries[2].Trustee.ptstrName = (LPWSTR)users;
    if (SetEntriesInAclW(3, entries, NULL, &acl) != ERROR_SUCCESS || !acl)
        goto done;
    result = SetNamedSecurityInfoW(path, SE_FILE_OBJECT,
                                   DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, NULL, NULL, acl,
                                   NULL);
    code = result == ERROR_SUCCESS ? 0 : (int)result;
done:
    if (acl)
        LocalFree(acl);
    if (system)
        FreeSid(system);
    if (admins)
        FreeSid(admins);
    if (users)
        FreeSid(users);
    return code;
}

int fr_helper_protect_directory(wchar_t* path) {
    return protect_object(path, OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE);
}

int fr_helper_protect_file(wchar_t* path) {
    return protect_object(path, NO_INHERITANCE);
}

static int process_alive(DWORD pid) {
    HANDLE process;
    DWORD code = 0;
    if (!pid)
        return 0;
    process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
        return GetLastError() == ERROR_ACCESS_DENIED;
    if (!GetExitCodeProcess(process, &code))
        code = STILL_ACTIVE;
    CloseHandle(process);
    return code == STILL_ACTIVE;
}

static DWORD parent_of(DWORD pid) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W entry;
    DWORD parent = 0;
    if (snap == INVALID_HANDLE_VALUE)
        return 0;
    entry.dwSize = sizeof entry;
    if (Process32FirstW(snap, &entry)) {
        do {
            if (entry.th32ProcessID == pid) {
                parent = entry.th32ParentProcessID;
                break;
            }
        } while (Process32NextW(snap, &entry));
    }
    CloseHandle(snap);
    return parent;
}

static int protected_helper_image(DWORD pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    wchar_t path[1024];
    DWORD size = 1024;
    wchar_t* slash;
    int ok = 0;
    if (!process)
        return 0;
    if (QueryFullProcessImageNameW(process, 0, path, &size)) {
        slash = wcsrchr(path, L'\\');
        if (slash && !_wcsicmp(slash + 1, L"virtual_display_helper.exe")) {
            ok = !fr_helper_object_user_writable(path);
            *slash = 0;
            ok = ok && !fr_helper_directory_user_writable(path);
        }
    }
    CloseHandle(process);
    return ok;
}

static int temp_dir(wchar_t* out, size_t cap) {
    DWORD n = GetTempPathW((DWORD)cap, out);
    return n > 0 && n < cap;
}

static int join_path(wchar_t* out, size_t cap, const wchar_t* dir, const wchar_t* leaf) {
    size_t n = wcslen(dir);
    int slash = n && (dir[n - 1] == L'\\' || dir[n - 1] == L'/');
    if (n + wcslen(leaf) + 2 >= cap)
        return 0;
    if (slash)
        swprintf(out, cap, L"%ls%ls", dir, leaf);
    else
        swprintf(out, cap, L"%ls\\%ls", dir, leaf);
    return 1;
}

static unsigned long read_pid_file(const wchar_t* path) {
    FILE* file = _wfopen(path, L"r");
    unsigned long pid = 0;
    if (!file)
        return 0;
    if (fwscanf(file, L"%lu", &pid) != 1)
        pid = 0;
    fclose(file);
    return pid;
}

static void write_pid_file(const wchar_t* path, unsigned long pid) {
    FILE* file = _wfopen(path, L"w");
    if (!file)
        return;
    fwprintf(file, L"%lu\n", pid);
    fclose(file);
}

static int ensure_dir(const wchar_t* path) {
    return CreateDirectoryW(path, NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
}

static int state_dir(wchar_t* out, size_t cap) {
    wchar_t root[MAX_PATH], parent[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"ProgramData", root, MAX_PATH);
    if (!n || n >= MAX_PATH)
        return 0;
    if (!join_path(parent, MAX_PATH, root, L"Fukaros") || !join_path(out, cap, parent, L"virtual-display-helper"))
        return 0;
    return 1;
}

static void delete_children(const wchar_t* dir) {
    wchar_t pattern[MAX_PATH], path[MAX_PATH];
    WIN32_FIND_DATAW data;
    HANDLE find;
    if (!join_path(pattern, MAX_PATH, dir, L"*"))
        return;
    find = FindFirstFileW(pattern, &data);
    if (find == INVALID_HANDLE_VALUE)
        return;
    do {
        if (!wcscmp(data.cFileName, L".") || !wcscmp(data.cFileName, L".."))
            continue;
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;
        if (join_path(path, MAX_PATH, dir, data.cFileName))
            DeleteFileW(path);
    } while (FindNextFileW(find, &data));
    FindClose(find);
}

static void delete_temp_prefix(void) {
    wchar_t dir[MAX_PATH], pattern[MAX_PATH], path[MAX_PATH];
    WIN32_FIND_DATAW data;
    HANDLE find;
    if (!temp_dir(dir, MAX_PATH))
        return;
    if (wcslen(dir) + 16 >= MAX_PATH)
        return;
    swprintf(pattern, MAX_PATH, L"%lsFukaros-vdh-*", dir);
    find = FindFirstFileW(pattern, &data);
    if (find == INVALID_HANDLE_VALUE)
        return;
    do {
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;
        if (join_path(path, MAX_PATH, dir, data.cFileName))
            DeleteFileW(path);
    } while (FindNextFileW(find, &data));
    FindClose(find);
}

static int stager_pid_path(wchar_t* path, size_t cap) {
    wchar_t dir[MAX_PATH];
    if (!temp_dir(dir, MAX_PATH))
        return 0;
    return join_path(path, cap, dir, L"Fukaros-vdh.pid");
}

static int own_pid_path(wchar_t* path, size_t cap) {
    wchar_t dir[MAX_PATH];
    if (!state_dir(dir, MAX_PATH))
        return 0;
    return join_path(path, cap, dir, L"helper.pid");
}

void fr_helper_note_own_pid(void) {
    wchar_t root[MAX_PATH], parent[MAX_PATH], dir[MAX_PATH], path[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"ProgramData", root, MAX_PATH);
    if (!n || n >= MAX_PATH)
        return;
    if (!join_path(parent, MAX_PATH, root, L"Fukaros") || !join_path(dir, MAX_PATH, parent, L"virtual-display-helper"))
        return;
    ensure_dir(parent);
    ensure_dir(dir);
    if (!join_path(path, MAX_PATH, dir, L"helper.pid"))
        return;
    write_pid_file(path, GetCurrentProcessId());
}

void fr_helper_clear_own_pid(void) {
    wchar_t path[MAX_PATH];
    if (own_pid_path(path, MAX_PATH))
        DeleteFileW(path);
}

void fr_helper_note_stager_pid(unsigned long pid) {
    wchar_t path[MAX_PATH];
    if (stager_pid_path(path, MAX_PATH))
        write_pid_file(path, pid);
}

void fr_helper_clear_install_pid(void) {
    wchar_t path[MAX_PATH];
    if (stager_pid_path(path, MAX_PATH))
        DeleteFileW(path);
}

void fr_helper_reap_protected_helper(void) {
    wchar_t dir[MAX_PATH], pid_path[MAX_PATH], tmp[MAX_PATH];
    unsigned long pid;
    int alive = 0, parent_known = 0, parent_alive = 0;
    if (state_dir(dir, MAX_PATH) && join_path(pid_path, MAX_PATH, dir, L"helper.pid")) {
        pid = read_pid_file(pid_path);
        if (pid)
            alive = process_alive((DWORD)pid);
        if (alive) {
            DWORD parent = parent_of((DWORD)pid);
            if (parent) {
                parent_known = 1;
                parent_alive = process_alive(parent);
            }
        }
        if (pid && pid != GetCurrentProcessId() &&
            (!alive || (parent_known && fr_helper_residue_stale(1, alive, parent_alive)))) {
            if (alive && protected_helper_image((DWORD)pid)) {
                HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, (DWORD)pid);
                if (process) {
                    TerminateProcess(process, 15);
                    WaitForSingleObject(process, 5000);
                    CloseHandle(process);
                }
            }
            DeleteFileW(pid_path);
        }
        if (join_path(tmp, MAX_PATH, dir, L"tmp"))
            delete_children(tmp);
    }
}

int fr_helper_stager_busy(void) {
    wchar_t path[MAX_PATH];
    unsigned long pid;
    if (!stager_pid_path(path, MAX_PATH))
        return 0;
    pid = read_pid_file(path);
    if (!pid)
        return 0;
    if (!process_alive((DWORD)pid)) {
        DeleteFileW(path);
        delete_temp_prefix();
        return 0;
    }
    return 1;
}

void fr_helper_cleanup_user_residue(void) {
    fr_helper_reap_protected_helper();
    if (!fr_helper_stager_busy())
        delete_temp_prefix();
}
#endif
