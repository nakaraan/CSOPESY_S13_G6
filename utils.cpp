// utils.cpp
#include "utils.h"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <csignal>
#include <iomanip>
#include <chrono>
#include <atomic>
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

// Forward declaration of global variable from globals.cpp
extern std::atomic<bool> is_running;

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

// Check if a number is a power of 2
bool is_power_of_two(size_t n) {
    return n > 0 && (n & (n - 1)) == 0;
}

// Validate memory size: must be power of 2, in range [64, 65536]
bool is_valid_memory_size(size_t size) {
    if (size < 64 || size > 65536) return false;
    return is_power_of_two(size);
}

// Parse hexadecimal string (with or without 0x prefix) to size_t
bool parse_hex_address(const std::string& hexStr, size_t& outAddress) {
    if (hexStr.empty()) return false;
    
    std::string str = hexStr;
    // Remove 0x or 0X prefix if present
    if (str.size() >= 2 && str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
        str = str.substr(2);
    }
    
    if (str.empty()) return false;
    
    // Check all characters are valid hex digits
    for (char c : str) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    }
    
    try {
        outAddress = std::stoull(str, nullptr, 16);
        return true;
    } catch (...) {
        return false;
    }
}

// Parse integer string to int
bool parse_integer(const std::string& str, int& outValue) {
    if (str.empty()) return false;
    try {
        size_t pos;
        outValue = std::stoi(str, &pos);
        return pos == str.size();  // Ensure entire string was consumed
    } catch (...) {
        return false;
    }
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
    // Enable ANSI escape sequence support on Windows 10+
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) return;

    DWORD mode = 0;
    if (!GetConsoleMode(hOut, &mode)) return;

    // Enable virtual terminal processing (ANSI escape codes)
    mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, mode);
}
#endif

// Parse user-defined instructions from command string
#include "process.h"
#include <sstream>
#include <algorithm>
#include <cctype>

static std::string trim(const std::string& str) {
    size_t start = 0;
    while (start < str.size() && std::isspace(str[start])) start++;
    size_t end = str.size();
    while (end > start && std::isspace(str[end - 1])) end--;
    return str.substr(start, end - start);
}

static std::vector<std::string> splitByDelimiter(const std::string& str, char delim) {
    std::vector<std::string> tokens;
    std::string current;
    bool inQuotes = false;
    
    for (char c : str) {
        if (c == '"') {
            inQuotes = !inQuotes;
            current += c;
        } else if (c == delim && !inQuotes) {
            if (!current.empty()) {
                tokens.push_back(trim(current));
                current.clear();
            }
        } else {
            current += c;
        }
    }
    
    if (!current.empty()) {
        tokens.push_back(trim(current));
    }
    
    return tokens;
}

std::vector<Instruction> parseUserInstructions(const std::string& instructionString) {
    std::vector<Instruction> instructions;
    
    // Split by semicolon
    std::vector<std::string> instrLines = splitByDelimiter(instructionString, ';');
    
    for (const auto& line : instrLines) {
        if (line.empty()) continue;
        
        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;
        
        // Convert to uppercase for comparison
        std::string upperCmd = cmd;
        for (char& c : upperCmd) c = std::toupper(c);
        
        Instruction instr;
        
        if (upperCmd == "PRINT") {
            instr.type = PRINT;
            // Get rest of line as print argument
            std::string rest;
            std::getline(iss, rest);
            instr.arg1 = trim(rest);
            instructions.push_back(instr);
        }
        else if (upperCmd == "DECLARE") {
            instr.type = DECLARE;
            iss >> instr.arg1 >> instr.val1;
            instructions.push_back(instr);
        }
        else if (upperCmd == "ADD") {
            instr.type = ADD;
            iss >> instr.arg1 >> instr.arg2 >> instr.arg3;
            instructions.push_back(instr);
        }
        else if (upperCmd == "SUBTRACT") {
            instr.type = SUBTRACT;
            iss >> instr.arg1 >> instr.arg2 >> instr.arg3;
            instructions.push_back(instr);
        }
        else if (upperCmd == "WRITE") {
            instr.type = WRITE_MEM;
            std::string addrStr;
            iss >> addrStr >> instr.arg2;  // WRITE <address> <variable>
            size_t addr;
            if (parse_hex_address(addrStr, addr)) {
                instr.arg1 = addrStr;  // Store hex address string
            }
            instructions.push_back(instr);
        }
        else if (upperCmd == "READ") {
            instr.type = READ_MEM;
            std::string addrStr;
            iss >> instr.arg1 >> addrStr;  // READ <variable> <address>
            size_t addr;
            if (parse_hex_address(addrStr, addr)) {
                instr.arg2 = addrStr;  // Store hex address string
            }
            instructions.push_back(instr);
        }
        else if (upperCmd == "SLEEP") {
            instr.type = SLEEP;
            iss >> instr.val1;
            instructions.push_back(instr);
        }
    }
    
    return instructions;
}
