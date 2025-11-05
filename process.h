#ifndef PROCESS_H
#define PROCESS_H
#include "utils.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

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
    std::vector<Instruction> instrSet; // used for FOR_LOOP to hold a set of instructions
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
    int nestingDepth =0; 
    std::unordered_map<std::string, uint16_t> memory;
    std::vector<std::string> logs;
};

void execute_instruction(ProcessControlBlock& pcb);

#endif