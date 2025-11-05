#include "globals.h"

// --- Shared State and Thread Control ---
std::atomic<bool> is_running{true};   // Global flag to signal threads to exit
std::atomic<bool> scheduler_running{false}; // initialized to false at the start

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

// config.txt parameters
int num_cpu = 4;
std::string scheduler_type = "fcfs";
int quantum_cycles = 5;
int batch_process_freq = 1;
int min_ins = 1000;
int max_ins = 2000;
int delay_per_exec = 0;

// Process management definitions
std::unordered_map<std::string, std::shared_ptr<ProcessControlBlock>> process_table;
std::vector<std::shared_ptr<ProcessControlBlock>> finished_processes;
std::queue<std::shared_ptr<ProcessControlBlock>> ready_queue;
std::mutex process_table_mutex;
std::condition_variable ready_cv;
bool initialized = false;
std::atomic<int> cpuCycles{0};