#ifndef GLOBALS_H
#define GLOBALS_H

#include <string>
#include <queue>
#include <mutex>
#include <atomic>
#include <unordered_map>
#include <vector>
#include <memory>
#include <condition_variable>

// Forward declarations
struct ProcessControlBlock;

// Global flags
extern std::atomic<bool> is_running;
extern std::atomic<bool> marquee_running;
extern std::atomic<int> marquee_speed;
extern std::atomic<int> marquee_position;
extern std::atomic<bool> scheduler_running;
extern std::atomic<bool> pause_display_refresh; // Control whether display auto-refreshes

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

// process management
extern std::unordered_map<std::string, std::shared_ptr<ProcessControlBlock>> process_table;
extern std::vector<std::shared_ptr<ProcessControlBlock>> finished_processes;
extern std::queue<std::shared_ptr<ProcessControlBlock>> ready_queue;
extern std::mutex process_table_mutex;
extern std::condition_variable ready_cv;
extern bool initialized;
extern std::atomic<int> cpuCycles;

#endif