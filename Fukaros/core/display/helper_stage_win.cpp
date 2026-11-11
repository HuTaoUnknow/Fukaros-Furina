#include "helper_launch.h"
#include "helper_hash.h"
#include <windows.h>
#include <shellapi.h>
#include <string.h>
#include <string>

static_assert(sizeof(wchar_t) == 2, "PowerShell EncodedCommand requires UTF-16");

static HANDLE g_launch_process = nullptr;
static HANDLE g_launch_lock = nullptr;

static std::wstring parent_dir(const std::wstring& path) {
    const auto pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos || pos == 0)
        return L"";
    return path.substr(0, pos);
}

static int copy_wide(wchar_t* out, size_t cap, const std::wstring& text) {
    if (text.size() + 1 > cap)
        return 0;
    memcpy(out, text.c_str(), (text.size() + 1) * sizeof(wchar_t));
    return 1;
}

static int hash_equal(const char* got) {
    const char* expect = FR_VIRTUAL_DISPLAY_HELPER_SHA256;
    int i;
    if (!got || strlen(expect) != 64 || strlen(got) != 64)
        return 0;
    for (i = 0; i < 64; i++) {
        char a = got[i], b = expect[i];
        if (a >= 'a' && a <= 'f')
            a = (char)(a - 32);
        if (b >= 'a' && b <= 'f')
            b = (char)(b - 32);
        if (a != b)
            return 0;
    }
    return 1;
}

static int only_hex64(const char* text) {
    int i;
    if (!text)
        return 0;
    for (i = 0; i < 64; i++) {
        const char c = text[i];
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F')))
            return 0;
    }
    return text[64] == 0;
}

static int hash_handle(HANDLE file, char out[65]) {
    fr_helper_sha256_ctx ctx;
    unsigned char buf[65536];
    DWORD n = 0;
    if (SetFilePointer(file, 0, nullptr, FILE_BEGIN) == INVALID_SET_FILE_POINTER && GetLastError() != NO_ERROR)
        return 0;
    fr_helper_sha256_init(&ctx);
    for (;;) {
        if (!ReadFile(file, buf, sizeof buf, &n, nullptr))
            return 0;
        if (!n)
            break;
        fr_helper_sha256_update(&ctx, buf, n);
    }
    fr_helper_sha256_final(&ctx, out);
    return 1;
}

static int current_elevated(void) {
    HANDLE token = nullptr;
    TOKEN_ELEVATION elevation{};
    DWORD size = 0;
    int elevated = 0;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return 0;
    if (GetTokenInformation(token, TokenElevation, &elevation, sizeof elevation, &size))
        elevated = elevation.TokenIsElevated ? 1 : 0;
    CloseHandle(token);
    return elevated;
}

static int protected_image(const wchar_t* path) {
    const std::wstring dir = parent_dir(path ? path : L"");
    if (!path || dir.empty())
        return 0;
    if (fr_helper_directory_user_writable(dir.c_str()))
        return 0;
    if (fr_helper_object_user_writable(path))
        return 0;
    return 1;
}

static int ensure_dir(const std::wstring& path) {
    return CreateDirectoryW(path.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

static int staged_paths(std::wstring* dir, std::wstring* exe) {
    wchar_t program_files[MAX_PATH];
    const DWORD n = GetEnvironmentVariableW(L"ProgramFiles", program_files, MAX_PATH);
    if (!n || n >= MAX_PATH)
        return 0;
    *dir = std::wstring(program_files) + L"\\Fukaros\\virtual-display-helper";
    *exe = *dir + L"\\virtual_display_helper.exe";
    return 1;
}

static int stage_copy(const wchar_t* source, const std::wstring& dest_dir, const std::wstring& dest) {
    const std::wstring parent = parent_dir(dest_dir);
    wchar_t dir_buf[1024], file_buf[1024];
    char hex[65];
    if (parent.empty() || !ensure_dir(parent) || !ensure_dir(dest_dir))
        return 15;
    if (!copy_wide(dir_buf, 1024, dest_dir) || fr_helper_protect_directory(dir_buf) != 0)
        return 15;
    if (fr_helper_directory_user_writable(dest_dir.c_str()))
        return 15;
    if (!CopyFileW(source, dest.c_str(), FALSE))
        return 15;
    if (!fr_helper_file_sha256(dest.c_str(), hex) || !hash_equal(hex)) {
        DeleteFileW(dest.c_str());
        return 11;
    }
    if (!copy_wide(file_buf, 1024, dest) || fr_helper_protect_file(file_buf) != 0 || !protected_image(dest.c_str())) {
        DeleteFileW(dest.c_str());
        return 15;
    }
    return 0;
}

static int runas_process(const wchar_t* exe, const wchar_t* params, const wchar_t* cwd, HANDLE* process) {
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof info;
    info.fMask = SEE_MASK_NOCLOSEPROCESS;
    info.lpVerb = L"runas";
    info.lpFile = exe;
    info.lpParameters = params;
    info.lpDirectory = cwd;
    info.nShow = SW_HIDE;
    if (!ShellExecuteExW(&info))
        return GetLastError() ? (int)GetLastError() : 12;
    *process = info.hProcess;
    return 0;
}

static int start_helper(const wchar_t* exe, const wchar_t* package, int elevated, HANDLE* process) {
    const std::wstring cwd = parent_dir(exe);
    if (!protected_image(exe) || cwd.empty())
        return 15;
    if (elevated) {
        wchar_t command[32768];
        STARTUPINFOW startup{};
        PROCESS_INFORMATION info{};
        startup.cb = sizeof startup;
        if (swprintf(command, 32768, L"\"%ls\" install \"%ls\"", exe, package) < 0)
            return 12;
        if (!CreateProcessW(exe, command, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, cwd.c_str(),
                            &startup, &info))
            return GetLastError() ? (int)GetLastError() : 12;
        CloseHandle(info.hThread);
        *process = info.hProcess;
        return 0;
    }
    const std::wstring params = std::wstring(L"install \"") + package + L"\"";
    return runas_process(exe, params.c_str(), cwd.c_str(), process);
}

static std::string base64_encode(const unsigned char* data, size_t length) {
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    size_t i = 0;
    while (i + 3 <= length) {
        const unsigned value = ((unsigned)data[i] << 16) | ((unsigned)data[i + 1] << 8) | data[i + 2];
        out.push_back(table[(value >> 18) & 63]);
        out.push_back(table[(value >> 12) & 63]);
        out.push_back(table[(value >> 6) & 63]);
        out.push_back(table[value & 63]);
        i += 3;
    }
    if (i < length) {
        unsigned value = (unsigned)data[i] << 16;
        out.push_back(table[(value >> 18) & 63]);
        if (i + 1 < length) {
            value |= (unsigned)data[i + 1] << 8;
            out.push_back(table[(value >> 12) & 63]);
            out.push_back(table[(value >> 6) & 63]);
            out.push_back('=');
        } else {
            out.push_back(table[(value >> 12) & 63]);
            out.push_back('=');
            out.push_back('=');
        }
    }
    return out;
}

static int only_base64(const std::string& text) {
    if (text.empty())
        return 0;
    for (char c : text) {
        const int ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '+' ||
                       c == '/' || c == '=';
        if (!ok)
            return 0;
    }
    return 1;
}

static std::string utf8_from_wide(const wchar_t* text) {
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    std::string out(size > 1 ? (size_t)size - 1 : 0, '\0');
    if (size > 1)
        WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), size, nullptr, nullptr);
    return out;
}

static int powershell_path(std::wstring* out) {
    wchar_t windows[MAX_PATH];
    const UINT n = GetWindowsDirectoryW(windows, MAX_PATH);
    BOOL wow = FALSE;
    if (!n || n >= MAX_PATH)
        return 0;
    if (IsWow64Process(GetCurrentProcess(), &wow) && wow)
        *out = std::wstring(windows) + L"\\Sysnative\\WindowsPowerShell\\v1.0\\powershell.exe";
    else
        *out = std::wstring(windows) + L"\\System32\\WindowsPowerShell\\v1.0\\powershell.exe";
    if (GetFileAttributesW(out->c_str()) == INVALID_FILE_ATTRIBUTES)
        return 0;
    /* System32 is the elevated host. Never substitute a helper path here. */
    return !fr_helper_directory_user_writable(parent_dir(*out).c_str());
}

static int start_powershell(const wchar_t* helper, const wchar_t* package, HANDLE* process) {
    std::wstring exe, cwd, script, params;
    const std::string source_utf8 = utf8_from_wide(helper);
    const std::string package_utf8 = utf8_from_wide(package);
    const std::string source =
        base64_encode(reinterpret_cast<const unsigned char*>(source_utf8.data()), source_utf8.size());
    const std::string packaged =
        base64_encode(reinterpret_cast<const unsigned char*>(package_utf8.data()), package_utf8.size());
    if (!only_hex64(FR_VIRTUAL_DISPLAY_HELPER_SHA256) || !only_base64(source) || !only_base64(packaged))
        return 11;
    if (!powershell_path(&exe))
        return 15;
    script =
        L"$ErrorActionPreference='Stop'\n"
        L"function Die([int]$c){[Environment]::Exit($c)}\n"
        L"try {\n"
        L"  $expected='@@HASH@@'\n"
        L"  if ($expected -notmatch '^[0-9A-F]{64}$') { Die 11 }\n"
        L"  $src=[Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('@@SRC@@'))\n"
        L"  $pkg=[Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('@@PKG@@'))\n"
        L"  $sha=[Security.Cryptography.SHA256]::Create()\n"
        L"  function HashFile([string]$path) {\n"
        L"    $fs=[IO.File]::Open($path,'Open','Read','Read')\n"
        L"    try { return (([BitConverter]::ToString($sha.ComputeHash($fs))).Replace('-','')) }\n"
        L"    finally { $fs.Dispose() }\n"
        L"  }\n"
        L"  if ((HashFile $src) -ne $expected) { Die 11 }\n"
        L"  $destDir=Join-Path $env:ProgramFiles 'Fukaros\\virtual-display-helper'\n"
        L"  [IO.Directory]::CreateDirectory($destDir) | Out-Null\n"
        L"  $icacls=Join-Path $env:SystemRoot 'System32\\icacls.exe'\n"
        L"  & $icacls $destDir /inheritance:r /grant:r '*S-1-5-18:(OI)(CI)F' '*S-1-5-32-544:(OI)(CI)F' "
        L"'*S-1-5-32-545:(OI)(CI)RX'\n"
        L"  if ($LASTEXITCODE -ne 0) { Die 15 }\n"
        L"  $dest=Join-Path $destDir 'virtual_display_helper.exe'\n"
        L"  if (Test-Path -LiteralPath $dest) { Remove-Item -LiteralPath $dest -Force }\n"
        L"  [IO.File]::Copy($src,$dest,$true)\n"
        L"  if ((HashFile $dest) -ne $expected) { Remove-Item -LiteralPath $dest -Force -ErrorAction "
        L"SilentlyContinue; Die 11 }\n"
        L"  & $icacls $dest /inheritance:r /grant:r '*S-1-5-18:F' '*S-1-5-32-544:F' '*S-1-5-32-545:RX'\n"
        L"  if ($LASTEXITCODE -ne 0) { Die 15 }\n"
        L"  $psi=New-Object Diagnostics.ProcessStartInfo\n"
        L"  $psi.FileName=$dest\n"
        L"  $psi.Arguments='install \"' + ($pkg.Replace('\"','')) + '\"'\n"
        L"  $psi.WorkingDirectory=$destDir\n"
        L"  $psi.UseShellExecute=$false\n"
        L"  $psi.CreateNoWindow=$true\n"
        L"  $proc=[Diagnostics.Process]::Start($psi)\n"
        L"  if (-not $proc) { Die 12 }\n"
        L"  $proc.WaitForExit()\n"
        L"  [Environment]::Exit($proc.ExitCode)\n"
        L"} catch { [Environment]::Exit(12) }\n";
    const std::wstring hash = std::wstring(FR_VIRTUAL_DISPLAY_HELPER_SHA256,
                                           FR_VIRTUAL_DISPLAY_HELPER_SHA256 + strlen(FR_VIRTUAL_DISPLAY_HELPER_SHA256));
    const std::wstring source_w(source.begin(), source.end());
    const std::wstring package_w(packaged.begin(), packaged.end());
    auto replace = [](std::wstring* text, const wchar_t* token, const std::wstring& value) {
        const auto pos = text->find(token);
        if (pos == std::wstring::npos)
            return 0;
        text->replace(pos, wcslen(token), value);
        return 1;
    };
    if (!replace(&script, L"@@HASH@@", hash) || !replace(&script, L"@@SRC@@", source_w) ||
        !replace(&script, L"@@PKG@@", package_w))
        return 12;
    const std::string encoded = base64_encode(reinterpret_cast<const unsigned char*>(script.c_str()),
                                              script.size() * sizeof(wchar_t));
    if (!only_base64(encoded) || encoded.size() > 24000)
        return 12;
    params = L"-NoProfile -NonInteractive -ExecutionPolicy Bypass -EncodedCommand ";
    params.append(encoded.begin(), encoded.end());
    cwd = parent_dir(exe);
    return runas_process(exe.c_str(), params.c_str(), cwd.c_str(), process);
}

extern "C" int fr_helper_start_install(const wchar_t* helper_path, const wchar_t* package_path, void** process,
                                      void** lock) {
    HANDLE file = INVALID_HANDLE_VALUE;
    HANDLE started = nullptr;
    char hex[65];
    std::wstring staged_dir, staged_exe, source_dir;
    int hash_ok, source_writable, staged_present = 0, staged_hash = 0, staged_writable = 1, choice, code;
    const wchar_t* launch = nullptr;
    const int elevated = current_elevated();
    if (process)
        *process = nullptr;
    if (lock)
        *lock = nullptr;
    if (!process || !lock || !helper_path || !package_path || !package_path[0] ||
        wcspbrk(package_path, L"\"\r\n") || package_path[wcslen(package_path) - 1] == L'\\')
        return 12;
    fr_helper_reap_launch_lock();
    fr_helper_cleanup_user_residue();
    if (fr_helper_stager_busy())
        return 16;
    file = CreateFileW(helper_path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                       nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return 11;
    if (!hash_handle(file, hex)) {
        CloseHandle(file);
        return 11;
    }
    hash_ok = hash_equal(hex);
    source_dir = parent_dir(helper_path);
    source_writable = source_dir.empty() || fr_helper_directory_user_writable(source_dir.c_str()) ||
                      fr_helper_object_user_writable(helper_path);
    if (!staged_paths(&staged_dir, &staged_exe)) {
        CloseHandle(file);
        return 15;
    }
    staged_present = GetFileAttributesW(staged_exe.c_str()) != INVALID_FILE_ATTRIBUTES;
    if (staged_present) {
        char staged_hex[65];
        staged_hash = fr_helper_file_sha256(staged_exe.c_str(), staged_hex) && hash_equal(staged_hex);
        staged_writable = fr_helper_directory_user_writable(staged_dir.c_str()) ||
                          fr_helper_object_user_writable(staged_exe.c_str());
    }
    choice = fr_helper_launch_decide(source_writable, hash_ok, staged_present, staged_hash, staged_writable);
    if (choice == FR_HELPER_LAUNCH_STAGED) {
        launch = staged_exe.c_str();
    } else if (choice == FR_HELPER_LAUNCH_IN_PLACE) {
        launch = helper_path;
    } else if (choice == FR_HELPER_LAUNCH_NEED_STAGE) {
        if (elevated) {
            code = stage_copy(helper_path, staged_dir, staged_exe);
            if (code) {
                CloseHandle(file);
                return code;
            }
            launch = staged_exe.c_str();
        } else {
            code = start_powershell(helper_path, package_path, &started);
            if (code) {
                CloseHandle(file);
                return code;
            }
            *process = started;
            *lock = file;
            fr_helper_note_stager_pid(GetProcessId(started));
            return 0;
        }
    } else {
        CloseHandle(file);
        return 11;
    }
    if (!launch || !protected_image(launch)) {
        CloseHandle(file);
        return 15;
    }
    /* The image loader must open this protected file. Its directory ACL, not a share lock,
     * is what stops a user from replacing it. The writable-source path keeps its lock. */
    CloseHandle(file);
    code = start_helper(launch, package_path, elevated, &started);
    if (code)
        return code;
    *process = started;
    *lock = nullptr;
    fr_helper_note_stager_pid(GetProcessId(started));
    return 0;
}

extern "C" void fr_helper_track_launch_lock(void* process, void* lock) {
    HANDLE duplicate = nullptr;
    fr_helper_reap_launch_lock();
    if (g_launch_process) {
        if (lock)
            CloseHandle((HANDLE)lock);
        return;
    }
    if (process && DuplicateHandle(GetCurrentProcess(), (HANDLE)process, GetCurrentProcess(), &duplicate, 0, FALSE,
                                   DUPLICATE_SAME_ACCESS))
        g_launch_process = duplicate;
    g_launch_lock = (HANDLE)lock;
}

extern "C" void fr_helper_reap_launch_lock(void) {
    DWORD code = 0;
    if (!g_launch_process)
        return;
    if (GetExitCodeProcess(g_launch_process, &code) && code == STILL_ACTIVE)
        return;
    CloseHandle(g_launch_process);
    g_launch_process = nullptr;
    if (g_launch_lock) {
        CloseHandle(g_launch_lock);
        g_launch_lock = nullptr;
    }
    fr_helper_clear_install_pid();
}
