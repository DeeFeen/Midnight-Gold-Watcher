#include "app.h"
#include <windows.h>
#include <cstdlib>
#include <cstdio>

std::wstring get_exe_dir() {
    wchar_t buf[MAX_PATH] = L"";
    DWORD n = GetModuleFileNameW(NULL, buf, MAX_PATH);
    std::wstring p(buf, n);
    size_t pos = p.find_last_of(L"\\/");
    return pos == std::wstring::npos ? L"." : p.substr(0, pos);
}

std::wstring get_ini_path() {
    return get_exe_dir() + L"\\GX_Monitor.ini";
}

std::wstring default_output_file() {
    // Prefer the GX addon folder (contains GX.toc) so the output stays where
    // watcher.py/OBS setups already point. Otherwise use the exe folder.
    std::wstring dir = get_exe_dir();
    for (int i = 0; i < 4; ++i) {
        std::wstring toc = dir + L"\\GX.toc";
        if (GetFileAttributesW(toc.c_str()) != INVALID_FILE_ATTRIBUTES)
            return dir + L"\\totalgold.txt";
        size_t pos = dir.find_last_of(L"\\/");
        if (pos == std::wstring::npos) break;
        std::wstring parent = dir.substr(0, pos);
        if (parent == dir) break;
        dir = parent;
    }
    return get_exe_dir() + L"\\totalgold.txt";
}

static std::wstring read_ini(const wchar_t* key, const wchar_t* def) {
    wchar_t buf[1024] = L"";
    GetPrivateProfileStringW(L"Settings", key, def, buf, 1024, get_ini_path().c_str());
    return std::wstring(buf);
}

static void write_ini(const wchar_t* key, const std::wstring& val) {
    WritePrivateProfileStringW(L"Settings", key, val.c_str(), get_ini_path().c_str());
}

bool load_settings(Settings& s) {
    s.wc.file = read_ini(L"file", L"");
    s.wc.output = read_ini(L"output", L"");
    s.wc.account = read_ini(L"account", L"");
    s.wc.wowRoot = read_ini(L"wowRoot", L"");
    std::wstring poll = read_ini(L"poll", L"2");
    s.wc.pollSeconds = wcstod(poll.c_str(), NULL);
    if (!(s.wc.pollSeconds >= 0.5)) s.wc.pollSeconds = 2.0;
    s.wc.raw = read_ini(L"raw", L"0") == L"1";
    s.startMinimized = read_ini(L"startMinimized", L"0") == L"1";
    if (s.wc.output.empty()) s.wc.output = default_output_file();
    return GetFileAttributesW(get_ini_path().c_str()) != INVALID_FILE_ATTRIBUTES;
}

void save_settings(const Settings& s) {
    write_ini(L"file", s.wc.file);
    write_ini(L"output", s.wc.output);
    write_ini(L"account", s.wc.account);
    write_ini(L"wowRoot", s.wc.wowRoot);
    wchar_t buf[64] = L"";
    swprintf(buf, 64, L"%g", s.wc.pollSeconds);
    write_ini(L"poll", buf);
    write_ini(L"raw", s.wc.raw ? L"1" : L"0");
    write_ini(L"startMinimized", s.startMinimized ? L"1" : L"0");
}

static const wchar_t RUN_KEY[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

void run_at_startup(bool enable) {
    HKEY key = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS)
        return;
    if (enable) {
        wchar_t exe[MAX_PATH] = L"";
        GetModuleFileNameW(NULL, exe, MAX_PATH);
        std::wstring cmd = L"\"" + std::wstring(exe) + L"\"";
        RegSetValueExW(key, L"GXGoldMonitor", 0, REG_SZ,
                       (const BYTE*)cmd.c_str(),
                       (DWORD)((cmd.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(key, L"GXGoldMonitor");
    }
    RegCloseKey(key);
}

bool run_at_startup_enabled() {
    HKEY key = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return false;
    DWORD type = 0, size = 0;
    LONG r = RegQueryValueExW(key, L"GXGoldMonitor", NULL, &type, NULL, &size);
    RegCloseKey(key);
    return r == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ);
}