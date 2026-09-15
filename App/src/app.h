#pragma once
#include <string>
#include "watch.h"

struct Settings {
    WatchConfig wc;
    bool startMinimized = false;
};

std::wstring get_exe_dir();
std::wstring default_output_file();
std::wstring get_ini_path();
bool load_settings(Settings& s);
void save_settings(const Settings& s);
void run_at_startup(bool enable);
bool run_at_startup_enabled();