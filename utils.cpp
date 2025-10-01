// utils.cpp
#include "utils.h"
#include "globals.h"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <csignal>
#ifndef _WIN32
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
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