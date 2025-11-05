#ifndef GLOBALS_H
#define GLOBALS_H

#include <string>
#include <queue>
#include <mutex>
#include <atomic>

// Global flags
extern std::atomic<bool> is_running;
extern std::atomic<bool> marquee_running;
extern std::atomic<int> marquee_speed;
extern std::atomic<int> marquee_position;
extern std::atomic<bool> scheduler_running;

// Shared buffers
extern std::string marquee_text;
extern std::string prompt_display_buffer;
extern std::string current_input;

// Mutexes
extern std::mutex marquee_to_display_mutex;
extern std::mutex prompt_mutex;
extern std::mutex command_queue_mutex;
extern std::mutex input_mutex;

// Command queue
extern std::queue<std::string> command_queue;

// config.txt parameters
extern int num_cpu;
extern std::string scheduler_type;
extern int quantum_cycles;
extern int batch_process_freq;
extern int min_ins;
extern int max_ins;
extern int delay_per_exec;


#endif