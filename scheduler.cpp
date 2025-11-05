#include "utils.h"
#include "process.h"
#include "globals.h"
#include <random>
#include <memory>
#include <string>
#include <vector>
#include <thread>
#include <chrono>

// Forward declaration
Instruction generate_random_instruction(int currentDepth, std::vector<std::string> declared_vars);

// Scheduler runtime state
static std::vector<std::thread> core_threads;
static std::thread generator_thread;
static std::thread sleep_watcher_thread;
static std::atomic<bool> scheduler_active{false};

std::shared_ptr<ProcessControlBlock> generate_random_process() {
    std::random_device rd;
    std::mt19937 gen(rd());
    
    std::uniform_int_distribution<> instruction_distrib(min_ins, max_ins);
    
    auto pcb = std::make_shared<ProcessControlBlock>();
    pcb->process = std::make_unique<Process>();
    pcb->process->pid = generate_pid();
    pcb->process->name = generate_process_name();
    pcb->processState = State::READY;

    int num_instructions = instruction_distrib(gen);
    std::vector<std::string> declared_vars;
    for (int i = 0; i < num_instructions; ++i) {
        Instruction instruction = generate_random_instruction(0, declared_vars);
        pcb->process->instructions.push_back(instruction);

        if (instruction.type == DECLARE) {
            declared_vars.push_back(instruction.arg1);
        }
    }

    return pcb;
}

Instruction generate_random_instruction(int currentDepth, std::vector<std::string> declared_vars) {
    std::random_device rd;
    std::mt19937 gen(rd());

    std::uniform_int_distribution<> uint16_distrib(0, 65535);
    std::uniform_int_distribution<> uint8_distrib(0, 255);
    std::uniform_int_distribution<> loop_iterations_distrib(1, 3);
    std::uniform_int_distribution<> loop_instructions_distrib(1, 5);
    std::uniform_int_distribution<> bool_distrib(0, 1);

    Instruction instruction;
    int instrType = (currentDepth >= 3) ? 4 : 5;
    std::uniform_int_distribution<> instruction_type_distrib(0, instrType);
    instruction.type = static_cast<InstructionType>(instruction_type_distrib(gen));

    switch (instruction.type) {
        case PRINT: {
            int print_choice = bool_distrib(gen);
            if (!declared_vars.empty() && print_choice == 1) {
                instruction.arg2 = "Value from: " + declared_vars[gen() % declared_vars.size()]; 
            }
            break;
        }
        case DECLARE: {
            instruction.arg1 = "var" + std::to_string(uint16_distrib(gen));
            instruction.val1 = uint16_distrib(gen);
            break;
        }
        case ADD:
        case SUBTRACT: {
            instruction.arg1 = "var" + std::to_string(uint16_distrib(gen));

            instruction.isLiteral1 = static_cast<bool>(bool_distrib(gen));
            if (instruction.isLiteral1) {
                instruction.val1 = uint16_distrib(gen);
            } else {
                instruction.arg2 = "var" + std::to_string(uint16_distrib(gen));
            }

            instruction.isLiteral2 = static_cast<bool>(bool_distrib(gen));
            if (instruction.isLiteral2) {
                instruction.val2 = uint16_distrib(gen);
            } else {
                instruction.arg3 = "var" + std::to_string(uint16_distrib(gen));
            }
            break;
        }
        case SLEEP: {
            instruction.val1 = uint8_distrib(gen);
            break;
        }
        case FOR_LOOP: {
            instruction.val1 = loop_iterations_distrib(gen);
            int instructionCount = loop_instructions_distrib(gen); 

            for (int i = 0; i < instructionCount; ++i) {
                Instruction loopInstruction = generate_random_instruction(currentDepth + 1, declared_vars);
                instruction.instrSet.push_back(loopInstruction);
            }
            break;
        }
    }

    return instruction;
}

void scheduler_start() {
    if (scheduler_active) return;
    scheduler_active = true;
    scheduler_running = true;

    // Generator thread
    generator_thread = std::thread([](){
        while (scheduler_active && is_running) {
            auto pcb = generate_random_process();
            {
                std::unique_lock<std::mutex> lock(process_table_mutex);
                process_table[pcb->process->name] = pcb;
                ready_queue.push(pcb);
            }
            ready_cv.notify_one();

            for (int i = 0; i < std::max(1, batch_process_freq) && scheduler_active && is_running; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                cpuCycles++;
            }
        }
    });

    // Sleep watcher thread
    sleep_watcher_thread = std::thread([](){
        while (scheduler_active && is_running) {
            {
                std::unique_lock<std::mutex> lock(process_table_mutex);
                for (auto &kv : process_table) {
                    auto &pcb = kv.second;
                    if (!pcb) continue;
                    if (pcb->processState == State::BLOCKED && pcb->sleepTicks > 0) {
                        pcb->sleepTicks--;
                        if (pcb->sleepTicks == 0) {
                            pcb->processState = State::READY;
                            ready_queue.push(pcb);
                            ready_cv.notify_one();
                        }
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    // Core worker threads
    core_threads.clear();
    for (int core = 0; core < std::max(1, num_cpu); ++core) {
        core_threads.emplace_back([core](){
            while (scheduler_active && is_running) {
                std::shared_ptr<ProcessControlBlock> pcb;
                {
                    std::unique_lock<std::mutex> lock(process_table_mutex);
                    if (ready_queue.empty()) {
                        ready_cv.wait_for(lock, std::chrono::milliseconds(10));
                        if (ready_queue.empty()) continue;
                    }
                    if (!ready_queue.empty()) {
                        pcb = ready_queue.front();
                        ready_queue.pop();
                    }
                }
                if (!pcb) continue;

                if (scheduler_type == "rr") {
                    int quantum = std::max(1, quantum_cycles);
                    for (int q = 0; q < quantum && pcb->processState != State::BLOCKED && pcb->processState != State::TERMINATED; ++q) {
                        execute_instruction(*pcb, core);
                        if (delay_per_exec > 0) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(delay_per_exec));
                        } else {
                            std::this_thread::sleep_for(std::chrono::milliseconds(1));
                        }
                        cpuCycles++;
                    }
                } else { // FCFS
                    while (pcb->processState != State::BLOCKED && pcb->processState != State::TERMINATED && scheduler_active && is_running) {
                        execute_instruction(*pcb, core);
                        if (delay_per_exec > 0) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(delay_per_exec));
                        } else {
                            std::this_thread::sleep_for(std::chrono::milliseconds(1));
                        }
                        cpuCycles++;
                    }
                }

                if (pcb->processState == State::TERMINATED) {
                    std::unique_lock<std::mutex> lock(process_table_mutex);
                    finished_processes.push_back(pcb);
                    process_table.erase(pcb->process->name);
                } else if (pcb->processState == State::READY) {
                    std::unique_lock<std::mutex> lock(process_table_mutex);
                    ready_queue.push(pcb);
                    ready_cv.notify_one();
                }
            }
        });
    }
}

void scheduler_stop() {
    if (!scheduler_active) return;
    scheduler_active = false;
    scheduler_running = false;
    ready_cv.notify_all();

    if (generator_thread.joinable()) generator_thread.join();
    if (sleep_watcher_thread.joinable()) sleep_watcher_thread.join();
    for (auto &t : core_threads) if (t.joinable()) t.join();
    core_threads.clear();
}