#ifndef PROCESS_H
#define PROCESS_H

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <memory>
#include "utils.h"

enum InstructionType {
    PRINT,
    DECLARE,
    ADD,
    SUBTRACT,
    SLEEP,
    FOR_LOOP,
    READ_MEM,
    WRITE_MEM
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

// Constants for memory management
constexpr size_t SYMBOL_TABLE_SIZE = 64;     // 64 bytes for symbol table
constexpr size_t MAX_VARIABLES = 32;          // Max 32 UINT16 variables (2 bytes each)
constexpr size_t MIN_MEMORY_SIZE = 64;        // Minimum 2^6 bytes
constexpr size_t MAX_MEMORY_SIZE = 65536;     // Maximum 2^16 bytes

struct Process {
    int pid;
    std::string name;
    std::vector<Instruction> instructions;
    size_t memorySize = 0;                     // Total allocated memory size
};

struct ProcessControlBlock {
    std::unique_ptr<Process> process;
    State processState = READY;
    int programCounter = 0;
    uint8_t sleepTicks = 0;
    int nestingDepth = 0; 
    std::unordered_map<std::string, uint16_t> memory;  // Legacy memory for DECLARE/ADD/SUBTRACT
    std::vector<std::string> logs;
    std::vector<Instruction> flattenedInstructions;
    bool isFlattened = false;
    
    // New memory management for READ/WRITE instructions
    std::vector<uint8_t> processMemory;                // Process memory buffer
    std::unordered_map<std::string, size_t> symbolTable;  // Variable name -> offset in symbol table segment
    size_t nextSymbolOffset = 0;                       // Next available offset in symbol table (0-62, step 2)
    
    // Initialize process memory with given size
    void initializeMemory(size_t size) {
        processMemory.resize(size, 0);  // Zero-initialized
        nextSymbolOffset = 0;
        symbolTable.clear();
    }
    
    // Get or create a variable in symbol table, returns offset or -1 if full
    int getOrCreateVariable(const std::string& varName) {
        auto it = symbolTable.find(varName);
        if (it != symbolTable.end()) {
            return static_cast<int>(it->second);
        }
        // Check if symbol table is full (max 32 variables, each 2 bytes = 64 bytes)
        if (nextSymbolOffset >= SYMBOL_TABLE_SIZE) {
            return -1;  // Symbol table full
        }
        size_t offset = nextSymbolOffset;
        symbolTable[varName] = offset;
        nextSymbolOffset += 2;  // Each UINT16 takes 2 bytes
        return static_cast<int>(offset);
    }
    
    // Read UINT16 from symbol table by variable name
    uint16_t readVariable(const std::string& varName) {
        auto it = symbolTable.find(varName);
        if (it == symbolTable.end()) {
            return 0;  // Uninitialized variable
        }
        size_t offset = it->second;
        if (offset + 1 >= processMemory.size()) return 0;
        return static_cast<uint16_t>(processMemory[offset]) |
               (static_cast<uint16_t>(processMemory[offset + 1]) << 8);
    }
    
    // Write UINT16 to symbol table by variable name
    bool writeVariable(const std::string& varName, uint16_t value) {
        int offset = getOrCreateVariable(varName);
        if (offset < 0) return false;  // Symbol table full
        processMemory[offset] = static_cast<uint8_t>(value & 0xFF);
        processMemory[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
        return true;
    }
    
    // Read UINT16 from memory address (data region starts after symbol table)
    uint16_t readMemoryAddress(size_t address) {
        if (address + 1 >= processMemory.size()) return 0;
        return static_cast<uint16_t>(processMemory[address]) |
               (static_cast<uint16_t>(processMemory[address + 1]) << 8);
    }
    
    // Write UINT16 to memory address
    bool writeMemoryAddress(size_t address, uint16_t value) {
        if (address + 1 >= processMemory.size()) return false;
        processMemory[address] = static_cast<uint8_t>(value & 0xFF);
        processMemory[address + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
        return true;
    }
};

void execute_instruction(ProcessControlBlock& pcb, int core_id);

#endif