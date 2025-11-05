#include "process.h"
#include "utils.h"
#include <algorithm>

void execute_instruction(ProcessControlBlock& pcb) {
    if (pcb.programCounter >= pcb.process->instructions.size()) {
        pcb.processState = State::TERMINATED;
        return;
    }

    Instruction& instruction = pcb.process->instructions[pcb.programCounter];
    pcb.processState = State::RUNNING;

    switch (instruction.type) {
        case PRINT: {
            // prints "Hello world from [process name]!" plus any additional message
            std::string output;
            if (!instruction.arg1.empty()) {
                output = instruction.arg1;
                if (pcb.memory.count(instruction.arg2)) {
                    output += std::to_string(pcb.memory[instruction.arg2]);
                } else {
                    output += instruction.arg2;
                }
            } else {
                output = "Hello world from " + pcb.process->name + "!";
            }
            pcb.logs.push_back(output);
            break;
        }
        case DECLARE: {
            // declares a uint16_t variable with a default value
            std::string varName = instruction.arg1;
            pcb.memory[varName] = clamp_uint16(instruction.val1);
            break;
        }
        case ADD: {
            // performs an addition operation where arg1 = dst, arg2/arg3 or val1/val2 = operands
            // automatically declares variables as 0 if they don't exist
            // clamped to uint16_t range
            std::string result = instruction.arg1;
            uint16_t op1 = 0;
            uint16_t op2 = 0;

            if (instruction.isLiteral1) {
                op1 = clamp_uint16(instruction.val1);
            } else {
                op1 = pcb.memory[instruction.arg2];
            }

            if (instruction.isLiteral2) {
                op2 = clamp_uint16(instruction.val2);
            } else {
                op2 = pcb.memory[instruction.arg3];
            }

            int sum = static_cast<int>(op1) + static_cast<int>(op2);
            pcb.memory[result] = clamp_uint16(sum);
            break;
        }
        case SUBTRACT: {
            // performs a subtraction operation where arg1 = dst, arg2/arg3 or val1/val2 = operands
            // automatically declares variables as 0 if they don't exist
            // clamped to uint16_t range
            std::string result = instruction.arg1;
            uint16_t op1 = 0;
            uint16_t op2 = 0;

            if (instruction.isLiteral1) {
                op1 = clamp_uint16(instruction.val1);
            } else {
                op1 = pcb.memory[instruction.arg2];
            }

            if (instruction.isLiteral2) {
                op2 = clamp_uint16(instruction.val2);
            } else {
                op2 = pcb.memory[instruction.arg3];
            }

            int difference = static_cast<int>(op1) - static_cast<int>(op2);
            pcb.memory[result] = clamp_uint16(difference);
            break;
        }
        case SLEEP: {
            // sleeps the current process for uint8 CPU ticks and relinquishes the CPU
            pcb.sleepTicks = static_cast<uint8_t>(std::min<uint16_t>(instruction.val1, 255)); // clamped to uint8_t
            pcb.processState = State::BLOCKED;
            break;
        }
        case FOR_LOOP: {
            // performs a for-loop given a set/array of instructions
            // can be nested up to 3 times
            if (pcb.nestingDepth >= 3) {
                pcb.logs.push_back("Error: Maximum FOR loop depth reached.");
                return;
            }
            
            pcb.nestingDepth++;
            for (int i = 0; i < instruction.val1; ++i) {
                for (Instruction& innerLoop : instruction.instrSet) {
                    execute_instruction(pcb);
                    if (pcb.processState == State::BLOCKED || pcb.processState == State::TERMINATED) {
                        pcb.nestingDepth--;
                        return; // exit if process is blocked or terminated
                    }
                }
            }
            pcb.nestingDepth--;
            break;
        }
    }

    pcb.programCounter++;
}