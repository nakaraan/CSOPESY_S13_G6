#include "process.h"
#include "utils.h"
#include <algorithm>

static bool flatten_instructions(const std::vector<Instruction>& instructions, std::vector<Instruction>& flatInst, int loopDepth = 0) {
    if (loopDepth > 3)
        return false; // prevents nesting beyond 3 levels

    for (const Instruction& instr : instructions) {
        if (instr.type != FOR_LOOP) { 
            flatInst.push_back(instr); // if instruction is not a FOR_LOOP, add it directly to the dst vector unchanged
        } else {
            if (loopDepth == 3)
                return false; 
            for (uint16_t i = 0; i < instr.val1; ++i) { 
                if (!flatten_instructions(instr.instrSet, flatInst, loopDepth + 1)) {
                    return false; 
                }
            }
        }
    }
    return true;
}

void execute_instruction(ProcessControlBlock& pcb) {
    if (pcb.processState == State::BLOCKED || pcb.sleepTicks > 0) { // returns early if the process is blocked/sleeping
        return;
    }

    if (!pcb.isFlattened) { // flattens instructions during the first execution (initial value of isFlattened is false)
        pcb.flattenedInstructions.clear(); 
        bool success = flatten_instructions(pcb.process->instructions, pcb.flattenedInstructions, 0);
        pcb.isFlattened = true;

        if (!success) {
            pcb.flattenedInstructions.clear();
            pcb.logs.push_back("Error: Maximum FOR_LOOP nesting depth exceeded.");
        }
        pcb.programCounter = 0;
    }
    
    if (pcb.programCounter < 0 || pcb.programCounter >= static_cast<int>(pcb.flattenedInstructions.size())) {
        pcb.processState = State::TERMINATED;
        return;
    }

    Instruction& instruction = pcb.flattenedInstructions[pcb.programCounter];
    pcb.processState = State::RUNNING;

    switch (instruction.type) {
        case PRINT: {
            // prints "Hello world from [process name]!" plus any additional message
            std::string output = "Hello world from " + pcb.process->name + "!";
            if (instruction.arg2.empty()) {
                output += " " + instruction.arg2;
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
            break;
        }
    }

    if (pcb.processState == State::BLOCKED || pcb.processState == State::TERMINATED)
        return;
    
    pcb.programCounter++;

    if (pcb.programCounter >= static_cast<int>(pcb.flattenedInstructions.size())) {
        pcb.processState = State::TERMINATED;
    } else {
        pcb.processState = State::READY;
    }
}