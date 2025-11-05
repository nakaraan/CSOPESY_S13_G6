#include "utils.h"
#include "process.h"
#include "globals.h"
#include <random>
#include <memory>
#include <string>
#include <vector>

std::unique_ptr<ProcessControlBlock> generate_random_process() {
    std::random_device rd;
    std::mt19937 gen(rd());
    
    std::uniform_int_distribution<> instruction_distrib(min_ins, max_ins);
    
    auto pcb = std::make_unique<ProcessControlBlock>();
    pcb->process = std::make_unique<Process>();
    pcb->process->pid = generate_pid();
    pcb->process->name = generate_process_name();
    pcb->processState = State::READY;

    int num_instructions = instruction_distrib(gen);
    std::vector<std::string> declared_vars; // to keep track of declared variables for PRINT instructions
    for (int i = 0; i < num_instructions; ++i) {
        Instruction instruction = generate_random_instruction(0, declared_vars);
        pcb->process->instructions.push_back(instruction);

        // update declared_vars if a DECLARE instruction is generated
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
    std::uniform_int_distribution<> loop_iterations_distrib(1, 3); // number of iterations for FOR_LOOP
    std::uniform_int_distribution<> loop_instructions_distrib(1, 5); // number of instructions inside FOR_LOOP
    std::uniform_int_distribution<> bool_distrib(0, 1); // 0 for false, 1 for true

    Instruction instruction;
    int instrType = (currentDepth >= 3) ? 4 : 5; // limits the instruction types to 4 (until SLEEP) if max depth is reached
    std::uniform_int_distribution<> instruction_type_distrib(0, instrType); // 0 for PRINT, 1 for DECLARE, etc.
    instruction.type = static_cast<InstructionType>(instruction_type_distrib(gen));

    switch (instruction.type) {
        case PRINT:
            // 50% chance to print just the default "Hello world from..." message or print with a variable 
            int print_choice = bool_distrib(gen);
            if (!declared_vars.empty() && print_choice == 1) {
                instruction.arg2 = "Value from: " + declared_vars[gen() % declared_vars.size()]; 
            }
            break;
        case DECLARE:
            instruction.arg1 = "var" + std::to_string(uint16_distrib(gen));
            instruction.val1 = uint16_distrib(gen);
            break;
        case ADD:
        case SUBTRACT:
            instruction.arg1 = "var" + std::to_string(uint16_distrib(gen));

            // operand 1
            instruction.isLiteral1 = static_cast<bool>(bool_distrib(gen));
            if (instruction.isLiteral1) {
                instruction.val1 = uint16_distrib(gen); // if true, assign a uint16 value
            } else {
                instruction.arg2 = "var" + std::to_string(uint16_distrib(gen)); // if false, assign a variable name
            }

            // operand 2
            instruction.isLiteral2 = static_cast<bool>(bool_distrib(gen));
            if (instruction.isLiteral2) {
                instruction.val2 = uint16_distrib(gen); // if true, assign a uint16 value
            } else {
                instruction.arg3 = "var" + std::to_string(uint16_distrib(gen)); // if false, assign a variable name
            }
            break;
        case SLEEP:
            instruction.val1 = uint8_distrib(gen);
            break;
        case FOR_LOOP:
            instruction.val1 = loop_iterations_distrib(gen);
            int instructionCount = loop_instructions_distrib(gen); 

            for (int i = 0; i < instructionCount; ++i) {
                Instruction loopInstruction = generate_random_instruction(currentDepth + 1, declared_vars);
                instruction.instrSet.push_back(loopInstruction);
            }

            break;
    }

    return instruction;
}

void scheduler_start() {
    scheduler_running = true;
    // TODO: Additional logic to start the scheduler
}

void scheduler_stop() {
    scheduler_running = false;
}