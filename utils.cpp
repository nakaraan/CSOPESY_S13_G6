// utils.cpp
#include "utils.h"
#include "globals.h"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <csignal>
#include <iomanip>
#include <chrono>
#ifndef _WIN32
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <cstdint>
#include <ctime>
#include <sstream>
#endif

#ifdef _WIN32
#include <windows.h>
#endif

std::string to_lowercase(const std::string &s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    return result;
}

std::vector<std::string> split_string(const std::string& str) { // splits the string into individual tokens
    std::istringstream iss(str);
    std::vector<std::string> tokens;
    std::string token;
    while (iss >> token) { // loops through the input string stream to extract words
        tokens.push_back(token); // adds the extracted word to the end of the vector
    }
    return tokens; // returns a vector of strings where each token becomes an element
}

uint16_t clamp_uint16 (int val) {
    if (val < 0) return 0;
    if (val > 65535) return 65535;
    return static_cast<uint16_t>(val);
}

std::string get_timestamp() { 
    auto now = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);

    std::tm local_tm{};

#ifdef _WIN32
    localtime_s(&local_tm, &now_time);
#else
    localtime_r(&now_time, &local_tm);
#endif
    std::ostringstream oss;
    oss << "("
        << std::put_time(&local_tm, "%m/%d/%Y %I:%M:%S")
        << (local_tm.tm_hour >= 12 ? " PM" : " AM") // indicate whether it's AM or PM
        << ")";

    return oss.str();
}

std::string log_format(int core_id, const std::string &instruction) {
    //FORMAT: (MM/DD/YYYY HH:MM:SSAM/PM) Core: [core_id] [instruction executed]
    std::ostringstream oss;
    oss << get_timestamp() << " Core: " << std::to_string(core_id) << " " << instruction;
    return oss.str();
}

int generate_pid() {
    static int pid = 1;
    return pid++;
}

std::string generate_process_name() {
    static int process_name = 1;
    std::ostringstream oss;
    oss << "p" << std::setw(2) << std::setfill('0') << process_name++;
    return oss.str();
}

// Simple SIGINT handler to cleanup nicely
void handle_sigint(int) {
    is_running = false;
#ifndef _WIN32
    disable_raw_mode();
#endif
}

// --- Terminal Raw Mode (POSIX) helpers ---
// <<< ADDED/FIXED >>>: For POSIX, enable raw (non-canonical, no-echo) mode so
// we can read characters as they come and render the typed line ourselves.
#ifndef _WIN32
struct TermiosGuard {
    struct termios orig;
    bool enabled = false;
} tguard;

void enable_raw_mode() {
    if (tguard.enabled) return;
    if (tcgetattr(STDIN_FILENO, &tguard.orig) == -1) return;
    struct termios raw = tguard.orig;
    raw.c_lflag &= ~(ECHO | ICANON); // disable echo and canonical (line) mode
    raw.c_iflag &= ~(IXON | ICRNL);  // disable ctrl-S/Q and CR->NL
    raw.c_cc[VMIN] = 0;              // non-blocking read (min bytes)
    raw.c_cc[VTIME] = 1;             // read timeout (0.1s)
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    // Make stdin non-blocking as well (so read won't forever block).
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
    tguard.enabled = true;
}

void disable_raw_mode() {
    if (!tguard.enabled) return;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &tguard.orig);
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    flags &= ~O_NONBLOCK;
    fcntl(STDIN_FILENO, F_SETFL, flags);
    tguard.enabled = false;
}
#endif

#ifdef _WIN32
void enable_windows_ansi() {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) return;

    DWORD mode = 0;
    if (!GetConsoleMode(hOut, &mode)) return;

    // Enable virtual terminal processing so ANSI escapes (clear screen, cursor) work.
    mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, mode);
}
#endif