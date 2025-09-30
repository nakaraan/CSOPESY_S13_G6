// utils.h
#ifndef UTILS_H
#define UTILS_H

#include <string>
#include <vector>

std::string to_lowercase(const std::string &s);
std::vector<std::string> split_string(const std::string& str);
void handle_sigint(int);

#ifndef _WIN32
void enable_raw_mode();
void disable_raw_mode();
#endif

#endif