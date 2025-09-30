#include "globals.h"

// --- Shared State and Thread Control ---
std::atomic<bool> is_running{true};   // Global flag to signal threads to exit

// Prompt messages (command feedback) shared between interpreter and display
std::string prompt_display_buffer = "";
std::mutex prompt_mutex;

// Queue of commands (from keyboard to command interpreter)
std::queue<std::string> command_queue;
std::mutex command_queue_mutex;

// Marquee state shared between logic + display
std::string marquee_text = "Welcome to CSOPESY!";
std::mutex marquee_to_display_mutex;
std::atomic<bool> marquee_running{false};
std::atomic<int> marquee_speed{300};     // in milliseconds
std::atomic<int> marquee_position{0};    // scroll position


// --- INPUT BUFFER ---
// <<< ADDED/FIXED >>>: store the partially-typed line here so the display
// thread can render it and not overwrite the user's typing.
std::string current_input;
std::mutex input_mutex;

// ^ISSUE ENCOUNTERED: whatever typed gets overwritten so nothing happens
