// utils.h
#ifndef UTILS_H
#define UTILS_H

#include <string>
#include <vector>
#include <cstdint>

std::string to_lowercase(const std::string &s);
std::vector<std::string> split_string(const std::string& str);
uint16_t clamp_uint16 (int val);
std::string get_timestamp();
std::string log_format(int core_id, const std::string &instruction);
int generate_pid();
std::string generate_process_name();
void handle_sigint(int);

#ifndef _WIN32
void enable_raw_mode();
void disable_raw_mode();
#endif

#ifdef _WIN32
void enable_windows_ansi();
#endif

#endif