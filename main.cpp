#include "globals.h"
#include "utils.h"
#include "keyboard.h"
#include "interpreter.h"
#include "marquee.h"
#include "display.h"
#include <thread>
#include <csignal>

int main() {
    std::signal(SIGINT, handle_sigint); // ensure cleanup on Ctrl-C

#ifdef _WIN32
    enable_windows_ansi(); // Enable ANSI escape sequences on Windows
#endif

#ifndef _WIN32
    // Ensure raw-mode disabled at exit even if something bad happens
    atexit([](){ disable_raw_mode(); });
#endif

    // Start threads
    std::thread marquee_logic_thread(marquee_logic_thread_func, 40);
    std::thread display_thread(display_thread_func);
    std::thread keyboard_handler_thread(keyboard_handler_thread_func);
    std::thread command_interpreter_thread(command_interpreter_thread_func);

    // Join all threads
    marquee_logic_thread.join();
    display_thread.join();
    keyboard_handler_thread.join();
    command_interpreter_thread.join();

#ifndef _WIN32
    // restore terminal if still in raw mode
    disable_raw_mode();
#endif

    return 0;
}
