#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include <vector>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <condition_variable>

struct GoldBreakdown {
    int64_t characters = 0;
    int64_t warband = 0;
    struct CharEntry { std::wstring name; int64_t gold = 0; };
    std::vector<CharEntry> chars;
    int64_t total() const { return characters + warband; }
};

struct WatchConfig {
    std::wstring file;        // full path to SavedVariables\GX.lua (empty = auto-detect)
    std::wstring wowRoot;     // WoW install root containing WTF (empty = walk up)
    std::wstring output;      // text file written for OBS
    std::wstring account;     // account folder filter (empty = first found)
    double pollSeconds = 2.0;
    bool raw = false;         // write bare copper number instead of "1,234g 56s 78c"
};

std::wstring format_num(long long v);
std::wstring format_gold(long long copper);
bool parse_gold_file(const std::wstring& path, GoldBreakdown& out);
bool find_wow_root(const std::wstring& startDir, std::wstring& out);
std::vector<std::wstring> discover_gold_files(const std::wstring& wowRoot, const std::wstring& accountFilter);
std::wstring auto_discover(const std::wstring& exeDir, const std::wstring& wowRoot, const std::wstring& accountFilter);
std::wstring path_leaf(const std::wstring& p);
std::wstring wstring_from_utf8(const std::string& u8);
std::string wstring_to_utf8(const std::wstring& w);

enum StatusKind { STATUS_WATCHING = 0, STATUS_WAITING = 1, STATUS_ERROR = 2 };

class Watcher {
public:
    using Callback = std::function<void(int64_t total, const std::wstring& formatted,
                                        const std::wstring& status, int statusKind,
                                        const GoldBreakdown& bd)>;
    void start(const std::wstring& exeDir, const WatchConfig& cfg, Callback cb);
    void stop();
    void setPaused(bool paused);
    void setConfig(const WatchConfig& cfg);
    void refresh();
    bool paused() const;

private:
    void loop();
    void do_one(const WatchConfig& cfg, bool force);

    std::thread th_;
    mutable std::mutex mu_;
    std::condition_variable cv_;
    WatchConfig cfg_;
    std::wstring exeDir_;
    Callback cb_;
    bool activated_ = false;
    bool paused_ = false;
    bool quit_ = false;
    bool cfgDirty_ = false;
    bool force_ = false;

    struct Key { long long mtime; long long size; };
    Key lastKey_{-1, -1};
    int64_t lastValue_ = -1;
    std::wstring lastOut_;
    bool wroteOnce_ = false;
};