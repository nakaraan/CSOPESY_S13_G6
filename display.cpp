// display.cpp
#include "display.h"
#include "globals.h"
#include <iostream>
#include <thread>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#endif

void clear_screen() {
#ifdef _WIN32
    // Use Windows API to clear screen reliably
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    DWORD count;
    DWORD cellCount;
    COORD homeCoords = {0, 0};

    if (hConsole == INVALID_HANDLE_VALUE) return;

    if (!GetConsoleScreenBufferInfo(hConsole, &csbi)) return;
    cellCount = csbi.dwSize.X * csbi.dwSize.Y;

    if (!FillConsoleOutputCharacter(hConsole, (TCHAR)' ', cellCount, homeCoords, &count)) return;

    if (!FillConsoleOutputAttribute(hConsole, csbi.wAttributes, cellCount, homeCoords, &count)) return;

    SetConsoleCursorPosition(hConsole, homeCoords);
#else
    // Use ANSI escape codes for POSIX systems
    std::cout << "\033[2J\033[H" << std::flush;
#endif
}

void display_thread_func() {
    const int refresh_rate_ms = 400; 
    const int display_width = 40;

    while (is_running) {
        std::string text_to_show;
        std::string prompt_message;
        std::string input_snapshot;

        // Get marquee text with thread-safe lock
        {
            std::unique_lock<std::mutex> lock(marquee_to_display_mutex);
            int pos = marquee_position;
            std::string padded = marquee_text + std::string(display_width, ' ');
            if (padded.empty()) padded = std::string(display_width, ' ');
            if (pos < 0) pos = 0;
            if (pos >= (int)padded.size()) {
                text_to_show = std::string(display_width, ' ');
            } else {
                // If substring requested crosses end, concatenate from start
                if (pos + display_width <= (int)padded.size()) {
                    text_to_show = padded.substr(pos, display_width);
                } else {
                    int first = padded.size() - pos;
                    text_to_show = padded.substr(pos, first);
                    text_to_show += padded.substr(0, display_width - first);
                }
            }
        }

        // Get latest prompt message
        {
            std::unique_lock<std::mutex> lock(prompt_mutex);
            prompt_message = prompt_display_buffer;
        }

        // Get current typed input snapshot so display doesn't wipe typing
        {
            std::unique_lock<std::mutex> lock(input_mutex);
            input_snapshot = current_input;
        }

        // Only clear and refresh if not paused (i.e., not in a submenu)
        if (!pause_display_refresh) {
            clear_screen();
            std::cout << "=========  OS Marquee Emulator  ========\n\n";
            std::cout << text_to_show << "\n\n";
            std::cout << "Type 'help' for commands.\n\n";
            std::cout << prompt_message << "\n";
            std::cout << "root:\\> " << input_snapshot << std::flush; // show typed text
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(refresh_rate_ms));
    }

    // When leaving the loop, print a final newline so terminal prompt looks clean
    std::cout << std::endl;
}