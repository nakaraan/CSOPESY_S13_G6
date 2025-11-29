#include "interpreter.h"
#include "globals.h"
#include "utils.h"
#include "process.h"
#include "memory.h"
#include <thread>
#include <chrono>
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>

// Forward declaration from scheduler.cpp
void scheduler_start();
void scheduler_test();
void scheduler_stop();
bool is_scheduler_active();
std::shared_ptr<ProcessControlBlock> generate_random_process(size_t memorySize = 256);

void command_interpreter_thread_func() {
    while (is_running) {
        std::string command_line;
        {
            std::unique_lock<std::mutex> lock(command_queue_mutex);
            if (!command_queue.empty()) {
                command_line = command_queue.front();
                command_queue.pop();
            }
        }

        if (!command_line.empty()) {
            std::vector<std::string> tokens = split_string(command_line);
            if (tokens.empty()) continue;
            std::string command = to_lowercase(tokens[0]);

            // Initialize command
            if (command == "initialize") {
                std::ifstream ifs("config.txt");
                if (!ifs) {
                    std::unique_lock<std::mutex> lock(prompt_mutex);
                    prompt_display_buffer = "Failed to open config.txt";
                } else {
                    std::string line;
                    while (std::getline(ifs, line)) {
                        std::istringstream iss(line);
                        std::string key;
                        iss >> key;
                        
                        if (key == "num-cpu") { iss >> num_cpu; }
                        else if (key == "scheduler") { 
                            std::string val; 
                            iss >> val;
                            // Remove quotes if present
                            if (val.front() == '"') val = val.substr(1);
                            if (val.back() == '"') val = val.substr(0, val.size()-1);
                            scheduler_type = val;
                        }
                        else if (key == "quantum-cycles") { iss >> quantum_cycles; }
                        else if (key == "batch-process-freq") { iss >> batch_process_freq; }
                        else if (key == "min-ins") { iss >> min_ins; }
                        else if (key == "max-ins") { iss >> max_ins; }
                        else if (key == "delay-per-exec") { iss >> delay_per_exec; }
                        else if (key == "max-overall-mem") { iss >> max_overall_mem; }
                        else if (key == "mem-per-frame") { iss >> mem_per_frame; }
                        else if (key == "min-mem-per-proc") { iss >> min_mem_per_proc; }
                        else if (key == "max-mem-per-proc") { iss >> max_mem_per_proc; }
                    }
                    
                    // Initialize memory manager with max_overall_mem converted to bytes
                    initializeMemory(max_overall_mem * 1024 * 1024);
                    
                    initialized = true;
                    scheduler_start();
                    std::unique_lock<std::mutex> lock(prompt_mutex);
                    prompt_display_buffer = "Initialized with " + std::to_string(num_cpu) + " CPUs, scheduler: " + scheduler_type;
                }
            }
            // Check if initialized before allowing other commands
            else if (!initialized && command != "exit" && command != "help") {
                std::unique_lock<std::mutex> lock(prompt_mutex);
                prompt_display_buffer = "Please run 'initialize' first.";
            }
            // Screen commands
            else if (command == "screen") {
                if (tokens.size() > 1) {
                    if (tokens[1] == "-s" && tokens.size() > 3) {
                        // screen -s <process_name> <process_memory_size>
                        std::string pname = tokens[2];
                        std::string pmemsize_str = tokens[3];
                        
                        // Parse memory size
                        int pmemsize_int = 0;
                        if (!parse_integer(pmemsize_str, pmemsize_int) || pmemsize_int < 0) {
                            std::unique_lock<std::mutex> lock(prompt_mutex);
                            prompt_display_buffer = "invalid memory allocation";
                        }
                        else {
                            size_t pmemsize = static_cast<size_t>(pmemsize_int);
                            
                            // Validate memory size: must be power of 2, in range [64, 65536]
                            if (!is_valid_memory_size(pmemsize)) {
                                std::unique_lock<std::mutex> lock(prompt_mutex);
                                prompt_display_buffer = "invalid memory allocation";
                            }
                            else {
                                // Valid memory size, create process
                                auto pcb = generate_random_process(pmemsize);
                                pcb->process->name = pname;
                                
                                // Allocate memory for the process
                                if (globalMemory) {
                                    if (!globalMemory->allocateProcess(pcb->process->pid, pmemsize)) {
                                        std::unique_lock<std::mutex> lock(prompt_mutex);
                                        prompt_display_buffer = "Failed to allocate memory for process " + pname;
                                        continue;
                                    }
                                }
                                
                                {
                                    std::unique_lock<std::mutex> lock(process_table_mutex);
                                    process_table[pname] = pcb;
                                    ready_queue.push(pcb);
                                }
                                ready_cv.notify_one();
                                std::unique_lock<std::mutex> lock(prompt_mutex);
                                prompt_display_buffer = "Process " + pname + " created with " + std::to_string(pmemsize) + " bytes.";
                            }
                        }
                    }
                    else if (tokens[1] == "-s" && tokens.size() > 2) {
                        // screen -s <process_name> (no memory size - use default 256)
                        std::string pname = tokens[2];
                        auto pcb = generate_random_process(256);  // Default 256 bytes
                        pcb->process->name = pname;
                        
                        // Allocate memory for the process
                        if (globalMemory) {
                            if (!globalMemory->allocateProcess(pcb->process->pid, 256)) {
                                std::unique_lock<std::mutex> lock(prompt_mutex);
                                prompt_display_buffer = "Failed to allocate memory for process " + pname;
                                continue;
                            }
                        }
                        
                        {
                            std::unique_lock<std::mutex> lock(process_table_mutex);
                            process_table[pname] = pcb;
                            ready_queue.push(pcb);
                        }
                        ready_cv.notify_one();
                        std::unique_lock<std::mutex> lock(prompt_mutex);
                        prompt_display_buffer = "Process " + pname + " created with 256 bytes (default).";
                    }
                    else if (tokens[1] == "-c" && tokens.size() > 3) {
                        // screen -c <process_name> <memory_size> "<instructions>"
                        std::string pname = tokens[2];
                        std::string pmemsize_str = tokens[3];
                        
                        // Parse memory size
                        int pmemsize_int = 0;
                        if (!parse_integer(pmemsize_str, pmemsize_int) || pmemsize_int < 0) {
                            std::unique_lock<std::mutex> lock(prompt_mutex);
                            prompt_display_buffer = "invalid memory allocation";
                        }
                        else {
                            size_t pmemsize = static_cast<size_t>(pmemsize_int);
                            
                            // Validate memory size
                            if (!is_valid_memory_size(pmemsize)) {
                                std::unique_lock<std::mutex> lock(prompt_mutex);
                                prompt_display_buffer = "invalid memory allocation";
                            }
                            else {
                                // Extract instruction string (everything after memory size, may be quoted)
                                std::string instructionStr;
                                size_t pos = command_line.find(tokens[3]);
                                if (pos != std::string::npos) {
                                    pos += tokens[3].length();
                                    instructionStr = command_line.substr(pos);
                                    // Trim leading/trailing whitespace and quotes
                                    size_t start = instructionStr.find_first_not_of(" \t\"");
                                    size_t end = instructionStr.find_last_not_of(" \t\"");
                                    if (start != std::string::npos && end != std::string::npos) {
                                        instructionStr = instructionStr.substr(start, end - start + 1);
                                    }
                                }
                                
                                // Parse user instructions
                                std::vector<Instruction> userInstructions = parseUserInstructions(instructionStr);
                                
                                // Validate instruction count (1-50)
                                if (userInstructions.empty() || userInstructions.size() > 50) {
                                    std::unique_lock<std::mutex> lock(prompt_mutex);
                                    prompt_display_buffer = "invalid command";
                                }
                                else {
                                    // Create process with user-defined instructions
                                    auto pcb = std::make_shared<ProcessControlBlock>();
                                    pcb->process = std::make_unique<Process>();
                                    pcb->process->pid = generate_pid();
                                    pcb->process->name = pname;
                                    pcb->process->instructions = userInstructions;
                                    pcb->process->memorySize = pmemsize;
                                    pcb->initializeMemory(pmemsize);
                                    
                                    // Allocate memory for the process
                                    if (globalMemory) {
                                        if (!globalMemory->allocateProcess(pcb->process->pid, pmemsize)) {
                                            std::unique_lock<std::mutex> lock(prompt_mutex);
                                            prompt_display_buffer = "Failed to allocate memory for process " + pname;
                                            continue;
                                        }
                                    }
                                    
                                    {
                                        std::unique_lock<std::mutex> lock(process_table_mutex);
                                        process_table[pname] = pcb;
                                        ready_queue.push(pcb);
                                    }
                                    ready_cv.notify_one();
                                    std::unique_lock<std::mutex> lock(prompt_mutex);
                                    prompt_display_buffer = "Process " + pname + " created with " + 
                                                           std::to_string(userInstructions.size()) + " user-defined instructions.";
                                }
                            }
                        }
                    }
                    else if (tokens[1] == "-ls") {
                        // Pause auto-refresh so user can scroll through the list
                        pause_display_refresh = true;
                        
                        std::ostringstream oss;
                        int cores_used = 0;
                        int cores_available = num_cpu;
                        
                        {
                            std::unique_lock<std::mutex> lock(process_table_mutex);
                            
                            // If scheduler is stopped, CPU utilization is 0%
                            if (!is_scheduler_active()) {
                                cores_used = 0;
                                cores_available = num_cpu;
                            } else {
                                cores_used = std::min((int)process_table.size(), num_cpu);
                                cores_available = num_cpu - cores_used;
                            }
                            
                            oss << "CPU utilization: " << (cores_used * 100 / std::max(1, num_cpu)) << "%\n";
                            oss << "Cores used: " << cores_used << "\n";
                            oss << "Cores available: " << cores_available << "\n\n";
                            oss << "Running processes:\n";
                            
                            for (auto &kv : process_table) {
                                auto &pcb = kv.second;
                                oss << pcb->process->name << "    ";
                                oss << get_timestamp() << "    ";
                                oss << "Core: " << (pcb->process->pid % num_cpu) << "    ";
                                int total_lines = pcb->flattenedInstructions.empty() ? pcb->process->instructions.size() : pcb->flattenedInstructions.size();
                                oss << pcb->programCounter << " / " << total_lines << "\n";
                            }
                            
                            oss << "\nFinished processes:\n";
                            for (auto &f : finished_processes) {
                                oss << f->process->name << "    ";
                                oss << get_timestamp() << "    ";
                                oss << "Finished    ";
                                oss << f->flattenedInstructions.size() << " / " << f->flattenedInstructions.size() << "\n";
                            }
                        }
                        
                        // Print directly to console since display refresh is paused
                        std::cout << "\n" << oss.str() << "\n";
                        std::cout << "Press any key to return to main menu..." << std::flush;
                        
                        // Wait for user input before returning
                        {
                            std::unique_lock<std::mutex> qlock(command_queue_mutex);
                            while (command_queue.empty() && is_running) {
                                qlock.unlock();
                                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                                qlock.lock();
                            }
                            if (!command_queue.empty()) {
                                command_queue.pop(); // consume the keypress
                            }
                        }
                        
                        // Resume auto-refresh when returning to main menu
                        pause_display_refresh = false;
                        std::unique_lock<std::mutex> lock(prompt_mutex);
                        prompt_display_buffer = "Returned to main menu.";
                    }
                    else if (tokens[1] == "-r" && tokens.size() > 2) {
                        std::string pname = tokens[2];
                        std::shared_ptr<ProcessControlBlock> pcb;
                        bool foundInFinished = false;
                        
                        {
                            std::unique_lock<std::mutex> lock(process_table_mutex);
                            auto it = process_table.find(pname);
                            if (it != process_table.end()) {
                                pcb = it->second;
                            } else {
                                // Check if in finished processes
                                for (const auto& fp : finished_processes) {
                                    if (fp->process->name == pname) {
                                        pcb = fp;
                                        foundInFinished = true;
                                        break;
                                    }
                                }
                            }
                        }
                        
                        if (!pcb) {
                            std::unique_lock<std::mutex> lock(prompt_mutex);
                            prompt_display_buffer = "Process " + pname + " not found.";
                        } else if (pcb->hasMemoryViolation) {
                            // Process shut down due to memory violation
                            std::ostringstream oss;
                            oss << "Process " << pname << " shut down due to memory access violation error that occurred at "
                                << pcb->memoryViolationTime << ". 0x" 
                                << std::hex << std::uppercase << pcb->memoryViolationAddress << " invalid";
                            std::unique_lock<std::mutex> lock(prompt_mutex);
                            prompt_display_buffer = oss.str();
                        } else {
                            // Pause display refresh while in interactive screen mode
                            pause_display_refresh = true;
                            
                            // Interactive screen mode
                            std::cout << "\nAttached to " << pname << ". Type 'process-smi' or 'exit'.\n> " << std::flush;

                            bool attached = true;
                            std::string last_input_shown = "";
                            
                            while (attached && is_running) {
                                // Display current input being typed
                                std::string current_input_copy;
                                {
                                    std::unique_lock<std::mutex> ilock(input_mutex);
                                    current_input_copy = current_input;
                                }
                                
                                if (current_input_copy != last_input_shown) {
                                    std::cout << "\r> " << current_input_copy << "    " << std::flush;
                                    last_input_shown = current_input_copy;
                                }
                                
                                std::string subcmd;
                                {
                                    std::unique_lock<std::mutex> qlock(command_queue_mutex);
                                    if (command_queue.empty()) {
                                        qlock.unlock();
                                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                                        continue;
                                    }
                                    subcmd = command_queue.front();
                                    command_queue.pop();
                                }
                                
                                last_input_shown = "";
                                
                                std::vector<std::string> subtokens = split_string(subcmd);
                                if (subtokens.empty()) {
                                    std::cout << "\n> " << std::flush;
                                    continue;
                                }
                                std::string scmd = to_lowercase(subtokens[0]);
                                
                                if (scmd == "process-smi") {
                                    std::ostringstream oss;
                                    oss << "Process name: " << pcb->process->name << "\n";
                                    oss << "ID: " << pcb->process->pid << "\n";
                                    oss << "Logs:\n";
                                    for (auto &l : pcb->logs) oss << l << "\n";
                                    oss << "\n";
                                    oss << "Current instruction line: " << pcb->programCounter << "\n";
                                    int total_lines = pcb->flattenedInstructions.empty() ? pcb->process->instructions.size() : pcb->flattenedInstructions.size();
                                    oss << "Lines of code: " << total_lines << "\n";
                                    if (pcb->processState == State::TERMINATED) oss << "\nFinished!\n";
                                    
                                    std::cout << "\n" << oss.str() << "\n> " << std::flush;
                                } else if (scmd == "exit") {
                                    attached = false;
                                    std::cout << "\nDetached from " << pname << "\n" << std::flush;
                                } else {
                                    std::cout << "\nUnknown command in screen. Use 'process-smi' or 'exit'.\n> " << std::flush;
                                }
                            }
                            
                            // Resume display refresh when exiting screen
                            pause_display_refresh = false;
                            std::unique_lock<std::mutex> lock(prompt_mutex);
                            prompt_display_buffer = "Returned to main menu.";
                        }
                    } else {
                        std::unique_lock<std::mutex> lock(prompt_mutex);
                        prompt_display_buffer = "Invalid screen arguments.";
                    }
                } else {
                    std::unique_lock<std::mutex> lock(prompt_mutex);
                    prompt_display_buffer = "Usage: screen -s <name> [mem_size] | screen -ls | screen -r <name>";
                }
            }
            // Scheduler commands
            else if (command == "scheduler-start") {
                scheduler_start();
                std::unique_lock<std::mutex> lock(prompt_mutex);
                prompt_display_buffer = "Scheduler started.";
            }
            else if (command == "scheduler-test") {
                scheduler_test();
                std::unique_lock<std::mutex> lock(prompt_mutex);
                prompt_display_buffer = "Scheduler test mode started.";
            }
            else if (command == "scheduler-stop") {
                scheduler_stop();
                std::unique_lock<std::mutex> lock(prompt_mutex);
                prompt_display_buffer = "Scheduler stopped.";
            }
            // Report utility
            else if (command == "report-util") {
                std::ofstream ofs("csopesy-log.txt");
                if (!ofs) {
                    std::unique_lock<std::mutex> lock(prompt_mutex);
                    prompt_display_buffer = "Failed to write report file.";
                } else {
                    std::unique_lock<std::mutex> lock(process_table_mutex);
                    int cores_used = std::min((int)process_table.size(), num_cpu);
                    int cores_available = num_cpu - cores_used;
                    
                    ofs << "CPU utilization: " << (cores_used * 100 / std::max(1, num_cpu)) << "%\n";
                    ofs << "Cores used: " << cores_used << "\n";
                    ofs << "Cores available: " << cores_available << "\n\n";
                    ofs << "Running processes:\n";
                    
                    for (auto &kv : process_table) {
                        auto &pcb = kv.second;
                        ofs << pcb->process->name << "    ";
                        ofs << get_timestamp() << "    ";
                        ofs << "Core: " << (pcb->process->pid % num_cpu) << "    ";
                        ofs << pcb->programCounter << " / " << pcb->flattenedInstructions.size() << "\n";
                    }
                    
                    ofs << "\nFinished processes:\n";
                    for (auto &f : finished_processes) {
                        ofs << f->process->name << "    ";
                        ofs << get_timestamp() << "    ";
                        ofs << "Finished    ";
                        ofs << f->flattenedInstructions.size() << " / " << f->flattenedInstructions.size() << "\n";
                    }
                    ofs.close();
                    
                    std::unique_lock<std::mutex> lock2(prompt_mutex);
                    prompt_display_buffer = "Report generated at csopesy-log.txt";
                }
            }
            // Memory debugging commands
            else if (command == "process-smi") {
                if (!globalMemory) {
                    std::unique_lock<std::mutex> lock(prompt_mutex);
                    prompt_display_buffer = "Memory manager not initialized. Run 'initialize' first.";
                } else {
                    MemoryStats stats = globalMemory->getStats();
                    auto processMemInfo = globalMemory->getAllProcessMemoryInfo();
                    
                    std::ostringstream oss;
                    oss << "=============================================\n";
                    oss << " PROCESS-SMI " << get_timestamp() << "\n";
                    oss << "=============================================\n";
                    oss << "CPU-Util: " << (std::min((int)process_table.size(), num_cpu) * 100 / std::max(1, num_cpu)) << "%\n";
                    
                    // Convert bytes to MiB (1 MiB = 1024*1024 bytes)
                    size_t usedMiB = stats.usedMemory / (1024 * 1024);
                    size_t totalMiB = stats.totalMemory / (1024 * 1024);
                    
                    oss << "Memory Usage: " << usedMiB << "MiB / " << totalMiB << "MiB\n";
                    oss << "Memory Util: " << (totalMiB > 0 ? (usedMiB * 100 / totalMiB) : 0) << "%\n";
                    oss << "=============================================\n";
                    oss << "Running processes and memory usage:\n";
                    oss << "---------------------------------------------\n";
                    
                    if (processMemInfo.empty()) {
                        oss << "No processes currently allocated in memory.\n";
                    } else {
                        // Look up process names from process table
                        std::unique_lock<std::mutex> lock(process_table_mutex);
                        for (const auto& info : processMemInfo) {
                            int pid = info.first;
                            size_t memBytes = info.second;
                            size_t memMiB = memBytes / (1024 * 1024);
                            if (memMiB == 0 && memBytes > 0) memMiB = 1; // Show at least 1 MiB if not zero
                            
                            // Find process name by PID
                            std::string processName = "process" + std::to_string(pid);
                            for (const auto& kv : process_table) {
                                if (kv.second->process->pid == pid) {
                                    processName = kv.first;
                                    break;
                                }
                            }
                            
                            oss << std::setw(15) << std::left << processName 
                                << std::setw(10) << std::right << memMiB << "MiB\n";
                        }
                    }
                    oss << "=============================================\n";
                    
                    std::unique_lock<std::mutex> lock(prompt_mutex);
                    prompt_display_buffer = oss.str();
                }
            }
            else if (command == "vmstat") {
                if (!globalMemory) {
                    std::unique_lock<std::mutex> lock(prompt_mutex);
                    prompt_display_buffer = "Memory manager not initialized. Run 'initialize' first.";
                } else {
                    MemoryStats stats = globalMemory->getStats();
                    
                    // Convert bytes to MiB
                    size_t totalMiB = stats.totalMemory / (1024 * 1024);
                    size_t usedMiB = stats.usedMemory / (1024 * 1024);
                    size_t freeMiB = stats.freeMemory / (1024 * 1024);
                    
                    std::ostringstream oss;
                    oss << "=============================================\n";
                    oss << " VMSTAT " << get_timestamp() << "\n";
                    oss << "=============================================\n";
                    oss << "Total memory: " << totalMiB << " MiB\n";
                    oss << "Used memory:  " << usedMiB << " MiB\n";
                    oss << "Free memory:  " << freeMiB << " MiB\n";
                    oss << "Idle cpu ticks: " << stats.idleCpuTicks << "\n";
                    oss << "Active cpu ticks: " << stats.activeCpuTicks << "\n";
                    oss << "Total cpu ticks: " << (stats.idleCpuTicks + stats.activeCpuTicks) << "\n";
                    oss << "Num paged in: " << stats.numPagedIn << "\n";
                    oss << "Num paged out: " << stats.numPagedOut << "\n";
                    oss << "=============================================\n";
                    
                    std::unique_lock<std::mutex> lock(prompt_mutex);
                    prompt_display_buffer = oss.str();
                }
            }
            else if (command == "help") {
                std::unique_lock<std::mutex> lock(prompt_mutex);
                prompt_display_buffer =
                    "Available commands:\n"
                    "initialize - read config.txt\n"
                    "screen -s <name> <mem_size> - create process (mem_size: 64-65536, power of 2)\n"
                    "screen -c <name> <mem_size> \"<instructions>\" - create process with custom instructions\n"
                    "screen -ls - list processes\n"
                    "screen -r <name> - attach to process\n"
                    "scheduler-start - start scheduler\n"
                    "scheduler-stop - stop scheduler\n"
                    "report-util - generate report\n"
                    "process-smi - show memory and process info\n"
                    "vmstat - show virtual memory statistics\n"
                    "start_marquee - start animation\n"
                    "stop_marquee - stop animation\n"
                    "set_text <text> - set marquee text\n"
                    "set_speed <ms> - set animation speed\n"
                    "exit - quit program";
            } else if (command == "start_marquee") {
                marquee_running = true;
                std::unique_lock<std::mutex> lock(prompt_mutex);
                prompt_display_buffer = "Marquee started.";
            } else if (command == "stop_marquee") {
                marquee_running = false;
                std::unique_lock<std::mutex> lock(prompt_mutex);
                prompt_display_buffer = "Marquee stopped.";
            } else if (command == "set_text") {
                if (tokens.size() > 1) {
                    std::string new_text;
                    for (size_t i = 1; i < tokens.size(); ++i) {
                        if (i > 1) new_text += " ";
                        new_text += tokens[i];
                    }
                    {
                        std::unique_lock<std::mutex> lock(marquee_to_display_mutex);
                        marquee_text = new_text;
                    }
                    marquee_position = 0;
                    std::unique_lock<std::mutex> lock(prompt_mutex);
                    prompt_display_buffer = "Marquee text updated to " + new_text;
                } else {
                    std::unique_lock<std::mutex> lock(prompt_mutex);
                    prompt_display_buffer = "No text parameter provided.";
                }
            } else if (command == "set_speed") {
                if (tokens.size() > 1) {
                    try {
                        int speed = std::stoi(tokens[1]);
                        if (speed > 0) {
                            marquee_speed = speed;
                            std::unique_lock<std::mutex> lock(prompt_mutex);
                            prompt_display_buffer = "Marquee speed set to " + std::to_string(speed) + " ms";
                        } else {
                            std::unique_lock<std::mutex> lock(prompt_mutex);
                            prompt_display_buffer = "Marquee speed must be a positive value.";
                        }
                    } catch (...) {
                        std::unique_lock<std::mutex> lock(prompt_mutex);
                        prompt_display_buffer = "Invalid speed value.";
                    }
                } else {
                    std::unique_lock<std::mutex> lock(prompt_mutex);
                    prompt_display_buffer = "No speed parameter provided.";
                }
            } else if (command == "exit") {
                is_running = false;
                std::unique_lock<std::mutex> lock(prompt_mutex);
                prompt_display_buffer = "Exiting console.";
            } else {
                std::unique_lock<std::mutex> lock(prompt_mutex);
                prompt_display_buffer = "Unknown command. Type 'help' for commands.";
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}