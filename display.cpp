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
    std::string last_prompt_message; // cache to prevent redundant heavy redraws
    size_t last_rendered_lines = 0;   // total lines last full render produced
    bool last_was_heavy_panel = false;
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

        // Decide if the current prompt content is a heavy multi-line static panel
        bool is_heavy_panel = (prompt_message.find("PROCESS-SMI") != std::string::npos) ||
                              (prompt_message.find("VMSTAT") != std::string::npos);
        bool prompt_changed = (prompt_message != last_prompt_message);

        // Only clear and redraw fully if:
        //  - not paused AND
        //  - prompt changed OR not heavy panel (marquee/status updates)
        if (!pause_display_refresh && (prompt_changed || !is_heavy_panel)) {
            clear_screen();
            std::cout << "=========  OS Marquee Emulator  ========\n\n";
            std::cout << text_to_show << "\n\n";
            std::cout << "Type 'help' for commands.\n\n";
            std::cout << prompt_message << "\n";
            std::cout << "root:\\> " << input_snapshot << std::flush;
            last_prompt_message = prompt_message; // update cache after redraw
            // Approximate line count for cursor math
            size_t panel_lines = 0;
            panel_lines += 2; // header + blank
            panel_lines += 2; // marquee + blank
            panel_lines += 2; // help + blank
            // count lines inside prompt_message
            panel_lines += 1; // at least one line
            for (char c : prompt_message) if (c == '\n') panel_lines++;
            panel_lines += 1; // prompt line itself
            last_rendered_lines = panel_lines;
            last_was_heavy_panel = is_heavy_panel;
        }
        // If heavy panel unchanged, do not redraw entire panel, only update input line so user can type.
        else if (!pause_display_refresh && is_heavy_panel && !prompt_changed) {
            // Update just the prompt/input line in place.
#ifdef _WIN32
            HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
            if (hConsole != INVALID_HANDLE_VALUE) {
                // Move cursor to start of prompt line (last_rendered_lines - 1)
                CONSOLE_SCREEN_BUFFER_INFO csbi; 
                if (GetConsoleScreenBufferInfo(hConsole, &csbi)) {
                    SHORT targetRow = (SHORT)std::min((int)csbi.dwSize.Y - 1, (int)(last_rendered_lines - 1));
                    COORD pos {0, targetRow};
                    SetConsoleCursorPosition(hConsole, pos);
                    // Clear the line then rewrite
                    DWORD written; 
                    std::string clearLine(csbi.dwSize.X, ' ');
                    WriteConsoleOutputCharacterA(hConsole, clearLine.c_str(), (DWORD)clearLine.size(), pos, &written);
                    SetConsoleCursorPosition(hConsole, pos);
                    std::cout << "root:\\> " << input_snapshot << std::flush;
                }
            }
#else
            // POSIX: Move cursor to line and clear then rewrite (1-based coordinates)
            size_t targetRow = last_rendered_lines; // prompt line approx
            std::cout << "\033[" << targetRow << ";1H"; // move
            std::cout << "\033[2K"; // clear line
            std::cout << "root:\\> " << input_snapshot << std::flush;
#endif
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(refresh_rate_ms));
    }

    // When leaving the loop, print a final newline so terminal prompt looks clean
    std::cout << std::endl;
}