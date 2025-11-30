// keyboard.cpp
#include "keyboard.h"
#include "globals.h"
#include "utils.h"
#include <thread>
#include <chrono>
#include <cctype>

#ifdef _WIN32
#include <conio.h>
#else
#include <unistd.h>
#endif

void keyboard_handler_thread_func() {
#ifdef _WIN32
    while (is_running) {
        if (_kbhit()) {
            int ch = _getch();
            std::unique_lock<std::mutex> lock(input_mutex);
            if (ch == 13) { // Enter
                std::string line = current_input;
                current_input.clear();
                lock.unlock();
                // Clear prompt display buffer when new command is entered
                {
                    std::unique_lock<std::mutex> plock(prompt_mutex);
                    prompt_display_buffer = "";
                }
                std::unique_lock<std::mutex> qlock(command_queue_mutex);
                command_queue.push(line);
            } else if (ch == 8 || ch == 127) { // Backspace
                if (!current_input.empty()) current_input.pop_back();
            } else if (ch == 3) { // Ctrl-C
                is_running = false;
            } else if (isprint(static_cast<unsigned char>(ch))) {
                current_input.push_back((char)ch);
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
#else
    enable_raw_mode();
    while (is_running) {
        char c = 0;
        ssize_t n = read(STDIN_FILENO, &c, 1);
        if (n > 0) {
            std::unique_lock<std::mutex> lock(input_mutex);
            if (c == '\n' || c == '\r') {
                std::string line = current_input;
                current_input.clear();
                lock.unlock();
                // Clear prompt display buffer when new command is entered
                {
                    std::unique_lock<std::mutex> plock(prompt_mutex);
                    prompt_display_buffer = "";
                }
                std::unique_lock<std::mutex> qlock(command_queue_mutex);
                command_queue.push(line);
            } else if (c == 127 || c == 8) { // Backspace
                if (!current_input.empty()) current_input.pop_back();
            } else if (c == 3) { // Ctrl-C
                is_running = false;
                break;
            } else if (isprint((unsigned char)c)) {
                current_input.push_back(c);
            }
        } else {
            // No input
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    disable_raw_mode();
#endif
}