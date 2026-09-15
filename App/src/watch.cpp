#include "watch.h"
#include <fstream>
#include <algorithm>
#include <map>
#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>

namespace {

std::string trim_str(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return std::string();
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

bool write_output(const std::wstring& path, const std::wstring& text) {
    if (path.empty()) return false;
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    std::string s = wstring_to_utf8(text);
    s.push_back('\n');
    DWORD wr = 0;
    BOOL ok = WriteFile(h, s.data(), (DWORD)s.size(), &wr, NULL);
    CloseHandle(h);
    return ok == TRUE && wr == (DWORD)s.size();
}

} // namespace

std::wstring wstring_from_utf8(const std::string& in) {
    if (in.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, in.data(), (int)in.size(), NULL, 0);
    if (n <= 0) return std::wstring();
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, in.data(), (int)in.size(), &w[0], n);
    return w;
}

std::string wstring_to_utf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), NULL, 0, NULL, NULL);
    if (n <= 0) return std::string();
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, NULL, NULL);
    return s;
}

std::wstring format_num(long long v) {
    bool neg = v < 0;
    unsigned long long n = neg ? (unsigned long long)(-(v + 1)) + 1ULL
                               : (unsigned long long)v;
    std::wstring s = std::to_wstring(n);
    std::wstring out;
    int cnt = 0;
    for (int i = (int)s.size() - 1; i >= 0; --i) {
        out.push_back(s[i]);
        if (++cnt % 3 == 0 && i > 0) out.push_back(L',');
    }
    std::reverse(out.begin(), out.end());
    if (neg) out = L"-" + out;
    return out;
}

std::wstring format_gold(long long copper) {
    long long c = copper < 0 ? 0 : copper;
    long long g = c / 10000;
    long long rem = c % 10000;
    long long s = rem / 100;
    long long cp = rem % 100;
    std::wstring out;
    if (g) { out += format_num(g); out += L"g"; }
    if (s) {
        if (!out.empty()) out += L" ";
        out += std::to_wstring(s); out += L"s";
    }
    if (cp || out.empty()) {
        if (!out.empty()) out += L" ";
        out += std::to_wstring(cp); out += L"c";
    }
    return out;
}

bool parse_gold_file(const std::wstring& path, GoldBreakdown& out) {
    out = GoldBreakdown();
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return false;

    std::vector<std::string> stack;
    std::map<std::string, int> idx;
    std::string line;

    auto in_warband = [&stack]() -> bool {
        for (const auto& t : stack) if (t == "warband") return true;
        return false;
    };

    while (std::getline(in, line)) {
        std::string t = trim_str(line);
        if (t.empty()) continue;

        if (t[0] == '}') {
            if (!stack.empty()) stack.pop_back();
            continue;
        }
        if (t[0] == '[') {
            auto cl = t.find("\"] = {");
            if (cl != std::string::npos) {
                std::string name = t.substr(2, cl - 2);
                stack.push_back(name);
                if (stack.size() >= 2 && stack[stack.size() - 2] == "characters") {
                    if (idx.find(name) == idx.end()) {
                        idx[name] = (int)out.chars.size();
                        out.chars.push_back({ wstring_from_utf8(name), 0 });
                    }
                }
                continue;
            }
            if (t.find("[\"gold\"] = ") == 0) {
                std::string num = t.substr(11);
                size_t k = num.find_first_of(",; ");
                if (k != std::string::npos) num = num.substr(0, k);
                char* endp = nullptr;
                long long v = std::strtoll(num.c_str(), &endp, 10);
                if (endp == num.c_str()) continue;
                if (in_warband()) {
                    out.warband += v;
                } else {
                    out.characters += v;
                    if (stack.size() >= 2 && stack[stack.size() - 2] == "characters") {
                        auto it = idx.find(stack.back());
                        if (it != idx.end()) out.chars[it->second].gold += v;
                    }
                }
            }
        }
    }
    return true;
}

std::wstring path_leaf(const std::wstring& p) {
    size_t pos = p.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return p;
    return p.substr(pos + 1);
}

bool find_wow_root(const std::wstring& start_dir, std::wstring& out) {
    std::wstring cur = start_dir;
    while (true) {
        std::wstring wtf = cur + L"\\WTF\\Account";
        if (GetFileAttributesW(wtf.c_str()) != INVALID_FILE_ATTRIBUTES) {
            out = cur;
            return true;
        }
        size_t pos = cur.find_last_of(L"\\/");
        if (pos == std::wstring::npos) return false;
        std::wstring parent = cur.substr(0, pos);
        if (parent == cur) return false;
        cur = parent;
    }
}

std::vector<std::wstring> discover_gold_files(const std::wstring& wow_root,
                                              const std::wstring& account_filter) {
    std::vector<std::wstring> roots;
    roots.push_back(wow_root);
    for (const wchar_t* flavor : { L"_retail_", L"_classic_", L"_classic_era_", L"_ptr_" })
        roots.push_back(wow_root + L"\\" + flavor);

    std::vector<std::wstring> found;
    for (const auto& root : roots) {
        std::wstring base = root + L"\\WTF\\Account";
        DWORD attrs = GetFileAttributesW(base.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY))
            continue;

        std::wstring pattern = base + L"\\*";
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;

        std::vector<std::wstring> accounts;
        do {
            if (fd.cFileName[0] == L'.') continue;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                accounts.push_back(fd.cFileName);
        } while (FindNextFileW(h, &fd));
        FindClose(h);

        std::sort(accounts.begin(), accounts.end());
        for (const auto& a : accounts) {
            if (!account_filter.empty() && a != account_filter) continue;
            std::wstring cand = base + L"\\" + a + L"\\SavedVariables\\GX.lua";
            if (GetFileAttributesW(cand.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
            bool dup = false;
            for (const auto& f : found) if (f == cand) { dup = true; break; }
            if (!dup) found.push_back(cand);
        }
    }
    return found;
}

std::wstring auto_discover(const std::wstring& exe_dir, const std::wstring& wow_root,
                           const std::wstring& account_filter) {
    std::vector<std::wstring> candidates;
    if (!wow_root.empty()) candidates = discover_gold_files(wow_root, account_filter);

    if (candidates.empty()) {
        std::wstring root;
        if (find_wow_root(exe_dir, root))
            candidates = discover_gold_files(root, account_filter);
    }
    if (candidates.empty()) {
        wchar_t cwd[MAX_PATH] = L"";
        GetCurrentDirectoryW(MAX_PATH, cwd);
        std::wstring root;
        if (find_wow_root(cwd, root))
            candidates = discover_gold_files(root, account_filter);
    }
    return candidates.empty() ? std::wstring() : candidates.front();
}

void Watcher::start(const std::wstring& exeDir, const WatchConfig& cfg, Callback cb) {
    {
        std::lock_guard<std::mutex> lk(mu_);
        exeDir_ = exeDir;
        cfg_ = cfg;
        cb_ = std::move(cb);
        activated_ = true;
        cfgDirty_ = true;
    }
    cv_.notify_all();
    if (!th_.joinable()) th_ = std::thread([this] { loop(); });
}

void Watcher::stop() {
    {
        std::lock_guard<std::mutex> lk(mu_);
        quit_ = true;
    }
    cv_.notify_all();
    if (th_.joinable()) th_.join();
}

void Watcher::setPaused(bool p) {
    {
        std::lock_guard<std::mutex> lk(mu_);
        paused_ = p;
    }
    cv_.notify_all();
}

bool Watcher::paused() const {
    std::lock_guard<std::mutex> lk(mu_);
    return paused_;
}

void Watcher::setConfig(const WatchConfig& cfg) {
    {
        std::lock_guard<std::mutex> lk(mu_);
        cfg_ = cfg;
        cfgDirty_ = true;
    }
    cv_.notify_all();
}

void Watcher::refresh() {
    {
        std::lock_guard<std::mutex> lk(mu_);
        force_ = true;
    }
    cv_.notify_all();
}

void Watcher::loop() {
    bool missing = false;
    bool forceNext = false;
    while (true) {
        std::unique_lock<std::mutex> lk(mu_);
        cv_.wait(lk, [&] { return quit_ || (activated_ && !paused_); });
        if (quit_) return;
        if (!activated_) continue;
        WatchConfig cfg = cfg_;
        lk.unlock();

        if (cfg.file.empty()) {
            std::wstring found = auto_discover(exeDir_, cfg.wowRoot, cfg.account);
            if (!found.empty()) {
                {
                    std::lock_guard<std::mutex> l2(mu_);
                    cfg_.file = found;
                }
                cfg.file = found;
            }
        }

        if (cfg.file.empty()) {
            if (!missing) {
                missing = true;
                if (cb_) cb_(-1, L"",
                              L"Waiting for SavedVariables — log in, run /gx show, then /reload",
                              STATUS_WAITING, GoldBreakdown());
            }
            for (int i = 0; i < 10; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                std::unique_lock<std::mutex> l2(mu_);
                if (quit_) return;
                if (paused_) break;
                if (force_) { force_ = false; forceNext = true; break; }
                if (cfgDirty_) { cfgDirty_ = false; break; }
            }
            continue;
        }
        missing = false;

        do_one(cfg, forceNext);
        forceNext = false;

        long long waitMs = (long long)(cfg.pollSeconds * 1000.0);
        if (waitMs < 500) waitMs = 500;
        long long elapsed = 0;
        while (elapsed < waitMs) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            std::unique_lock<std::mutex> l2(mu_);
            if (quit_) return;
            if (paused_) break;
            if (force_) { force_ = false; forceNext = true; break; }
            if (cfgDirty_) { cfgDirty_ = false; break; }
            l2.unlock();
            elapsed += 200;
        }
    }
}

void Watcher::do_one(const WatchConfig& cfg, bool force) {
    struct _stat64 st;
    if (_wstat64(cfg.file.c_str(), &st) != 0) {
        lastKey_ = Key{ -1, -1 };
        lastValue_ = -1;
        if (cb_) cb_(-1, L"",
                     L"Waiting for file… will resume when the game saves again",
                     STATUS_WAITING, GoldBreakdown());
        return;
    }

    Key key{ (long long)st.st_mtime, (long long)st.st_size };
    if (!force && key.mtime == lastKey_.mtime && key.size == lastKey_.size && wroteOnce_)
        return;

    GoldBreakdown bd;
    int64_t value = -1;
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (parse_gold_file(cfg.file, bd)) { value = bd.total(); break; }
        Sleep(1000);
    }
    if (value < 0) {
        if (cb_) cb_(-1, L"",
                     L"Could not read SavedVariables — file may be mid-write",
                     STATUS_ERROR, bd);
        return;
    }

    lastKey_ = key;
    bool out_changed = (cfg.output != lastOut_);
    bool write_now = force || out_changed || (value != lastValue_) || !wroteOnce_;
    lastValue_ = value;
    lastOut_ = cfg.output;

    std::wstring text = cfg.raw ? format_num(value) : format_gold(value);
    std::wstring status = L"Watching " + path_leaf(cfg.file);

    if (write_now) {
        if (!write_output(cfg.output, text)) {
            wroteOnce_ = false;
            if (cb_) cb_(value, text, L"Error writing " + cfg.output, STATUS_ERROR, bd);
            return;
        }
        wroteOnce_ = true;
    }
    if (cb_) cb_(value, text, status, STATUS_WATCHING, bd);
}