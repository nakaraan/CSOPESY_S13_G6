#include "utils.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <algorithm>

enum InstructionType {
    PRINT,
    DECLARE,
    ADD,
    SUBTRACT,
    SLEEP,
    FOR_LOOP
};

enum State {
    READY,
    RUNNING,
    BLOCKED,
    TERMINATED
};

struct Instruction { 
    InstructionType type;
    std::string arg1, arg2, arg3; // used for PRINT, DECLARE, ADD, SUBTRACT as messages or variables
    uint16_t val1, val2; // used for DECLARE, ADD, SUBTRACT, SLEEP (clamped to uint8_t), FOR_LOOP as numeric values
    bool isLiteral1 = false, isLiteral2 = false; // used for ADD and SUBTRACT to indicate if args are a literal (true if literal, false if variable)
};

struct Process {
    int pid;
    std::string name;
    std::vector<Instruction> instructions;
};

struct ProcessControlBlock {
    Process* process;
    State processState = READY;
    int programCounter = 0;
    uint8_t sleepTicks = 0;
    std::unordered_map<std::string, uint16_t> memory;
};

void execute_instruction(ProcessControlBlock& pcb) {
    if (pcb.programCounter >= pcb.process->instructions.size()) {
        pcb.processState = State::TERMINATED;
        return;
    }

    Instruction& instruction = pcb.process->instructions[pcb.programCounter];
    pcb.processState = RUNNING;

    switch (instruction.type) {
        case PRINT:
            // prints "Hello world from [process name]!" plus any additional message
            std::string msg = instruction.arg1;
            if (!instruction.arg2.empty()) {
                if (pcb.memory.count(instruction.arg2)) {
                    msg += std::to_string(pcb.memory[instruction.arg2]);
                } else {
                    msg += instruction.arg2;
                }
            }
            std::string output = "Hello world from " + pcb.process->name + "! " + msg;
            break;
        case DECLARE:
            // declares a uint16_t variable with a default value
            std::string varName = instruction.arg1;
            pcb.memory[varName] = clamp_uint16(instruction.val1);
            break;
        case ADD:
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
        case SUBTRACT:
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
        case SLEEP:
            // sleeps the current process for uint8 CPU ticks and relinquishes the CPU
            uint8_t sleepCounter = static_cast<uint8_t>(std::min<uint16_t>(instruction.val1, 255)); // clamped to uint8_t

            pcb.sleepTicks = sleepCounter;
            pcb.processState = State::BLOCKED;
            break;
        case FOR_LOOP:
            // Simulate for loop operation
            break;
    }

    pcb.programCounter++;
}

void scheduler_start() {
    
}