#include "process.h"
#include "utils.h"
#include "memory.h"
#include <algorithm>
#include <sstream>
#include <iomanip>

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

void execute_instruction(ProcessControlBlock& pcb, int core_id) {
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
            std::string output;
            
            if (!pcb.processMemory.empty() && !instruction.arg1.empty()) {
                // User-defined instruction with custom format
                // Parse PRINT("Result: " + varC) type syntax
                std::string printArg = instruction.arg1;
                
                // Remove parentheses if present
                if (printArg.front() == '(' && printArg.back() == ')') {
                    printArg = printArg.substr(1, printArg.length() - 2);
                }
                
                // Look for string concatenation with variables
                output = "";
                size_t pos = 0;
                bool inQuote = false;
                std::string current;
                
                for (size_t i = 0; i < printArg.size(); ++i) {
                    char c = printArg[i];
                    if (c == '"') {
                        if (inQuote) {
                            // End of string literal
                            output += current;
                            current.clear();
                            inQuote = false;
                        } else {
                            // Start of string literal
                            inQuote = true;
                        }
                    } else if (c == '+' && !inQuote) {
                        // Concatenation operator outside quotes - skip
                        continue;
                    } else if (inQuote) {
                        current += c;
                    } else if (!std::isspace(c)) {
                        // Variable name
                        current += c;
                        if (i + 1 >= printArg.size() || printArg[i+1] == '+' || printArg[i+1] == '"' || std::isspace(printArg[i+1])) {
                            // End of variable name
                            if (!current.empty()) {
                                uint16_t varVal = pcb.readVariable(current);
                                output += std::to_string(varVal);
                                current.clear();
                            }
                        }
                    }
                }
                
                // Add any remaining literal text
                if (!current.empty()) {
                    if (inQuote) {
                        output += current;
                    } else {
                        // Variable
                        uint16_t varVal = pcb.readVariable(current);
                        output += std::to_string(varVal);
                    }
                }
            } else {
                // Legacy random-generated process
                output = "Hello world from " + pcb.process->name + "!";
                if (!instruction.arg2.empty()) {
                    // Check if arg2 contains a variable reference
                    size_t pos = instruction.arg2.find("Value from: ");
                    if (pos != std::string::npos) {
                        std::string varName = instruction.arg2.substr(pos + 12);
                        if (pcb.memory.find(varName) != pcb.memory.end()) {
                            output += " Value from: " + std::to_string(pcb.memory[varName]);
                        } else {
                            output += " " + instruction.arg2;
                        }
                    } else {
                        output += " " + instruction.arg2;
                    }
                }
            }
            
            pcb.logs.push_back(log_format(core_id, output));
            break;
        }
        case DECLARE: {
            // declares a uint16_t variable with a default value
            std::string varName = instruction.arg1;
            uint16_t value = clamp_uint16(instruction.val1);
            
            // If process has initialized memory (user-defined instructions), use symbol table
            if (!pcb.processMemory.empty()) {
                // Access symbol table page through memory manager
                if (globalMemory) {
                    // Symbol table is at the beginning (address 0)
                    bool accessOk = globalMemory->accessMemory(pcb.process->pid, 0, true);
                    if (!accessOk) {
                        pcb.hasMemoryViolation = true;
                        pcb.memoryViolationTime = get_timestamp();
                        pcb.memoryViolationAddress = 0;
                        pcb.processState = State::TERMINATED;
                        pcb.logs.push_back(log_format(core_id, "Symbol table page fault - cannot declare variable"));
                        break;
                    }
                }
                
                if (!pcb.writeVariable(varName, value)) {
                    pcb.logs.push_back(log_format(core_id, "Error: Symbol table full, cannot create variable " + varName));
                }
            } else {
                // Legacy memory for random-generated processes
                pcb.memory[varName] = value;
            }
            break;
        }
        case ADD: {
            // performs an addition operation where arg1 = dst, arg2/arg3 or val1/val2 = operands
            // automatically declares variables as 0 if they don't exist
            // clamped to uint16_t range
            std::string result = instruction.arg1;
            uint16_t op1 = 0;
            uint16_t op2 = 0;

            if (!pcb.processMemory.empty()) {
                // Use symbol table for user-defined instructions
                op1 = pcb.readVariable(instruction.arg2);
                op2 = pcb.readVariable(instruction.arg3);
                
                int sum = static_cast<int>(op1) + static_cast<int>(op2);
                uint16_t resultVal = clamp_uint16(sum);
                
                if (!pcb.writeVariable(result, resultVal)) {
                    pcb.logs.push_back(log_format(core_id, "Error: Symbol table full, cannot store result"));
                }
            } else {
                // Legacy memory for random-generated processes
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
            }
            break;
        }
        case SUBTRACT: {
            // performs a subtraction operation where arg1 = dst, arg2/arg3 or val1/val2 = operands
            // automatically declares variables as 0 if they don't exist
            // clamped to uint16_t range
            std::string result = instruction.arg1;
            uint16_t op1 = 0;
            uint16_t op2 = 0;

            if (!pcb.processMemory.empty()) {
                // Use symbol table for user-defined instructions
                op1 = pcb.readVariable(instruction.arg2);
                op2 = pcb.readVariable(instruction.arg3);
                
                int diff = static_cast<int>(op1) - static_cast<int>(op2);
                uint16_t resultVal = clamp_uint16(diff);
                
                if (!pcb.writeVariable(result, resultVal)) {
                    pcb.logs.push_back(log_format(core_id, "Error: Symbol table full, cannot store result"));
                }
            } else {
                // Legacy memory for random-generated processes
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
            }
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
        case READ_MEM: {
            // READ var memory_address
            // Reads UINT16 from memory address and stores in variable
            std::string varName = instruction.arg1;
            std::string addrStr = instruction.arg2;
            
            // Parse hex address
            size_t address = 0;
            if (!parse_hex_address(addrStr, address)) {
                // Try parsing as decimal
                int addr_int = 0;
                if (parse_integer(addrStr, addr_int) && addr_int >= 0) {
                    address = static_cast<size_t>(addr_int);
                } else {
                    pcb.logs.push_back(log_format(core_id, "Error: Invalid address format " + addrStr));
                    break;
                }
            }
            
            // Check address bounds
            if (address + 1 >= pcb.processMemory.size()) {
                // Memory access violation
                pcb.hasMemoryViolation = true;
                pcb.memoryViolationTime = get_timestamp();
                pcb.memoryViolationAddress = address;
                pcb.processState = State::TERMINATED;
                std::ostringstream oss;
                oss << "Memory access violation at 0x" << std::hex << std::uppercase << address;
                pcb.logs.push_back(log_format(core_id, oss.str()));
                break;
            }
            
            // Access memory through memory manager (handles page faults)
            if (globalMemory) {
                bool accessOk = globalMemory->accessMemory(pcb.process->pid, address, false);
                if (!accessOk) {
                    pcb.hasMemoryViolation = true;
                    pcb.memoryViolationTime = get_timestamp();
                    pcb.memoryViolationAddress = address;
                    pcb.processState = State::TERMINATED;
                    pcb.logs.push_back(log_format(core_id, "Memory access failed"));
                    break;
                }
            }
            
            // Read value from memory address
            uint16_t value = pcb.readMemoryAddress(address);
            
            // Store in variable (symbol table)
            if (!pcb.writeVariable(varName, value)) {
                pcb.logs.push_back(log_format(core_id, "Error: Symbol table full, cannot create variable " + varName));
            }
            break;
        }
        case WRITE_MEM: {
            // WRITE memory_address variable
            // Writes UINT16 value from variable to memory address
            std::string addrStr = instruction.arg1;
            std::string varName = instruction.arg2;
            
            // Parse hex address
            size_t address = 0;
            if (!parse_hex_address(addrStr, address)) {
                // Try parsing as decimal
                int addr_int = 0;
                if (parse_integer(addrStr, addr_int) && addr_int >= 0) {
                    address = static_cast<size_t>(addr_int);
                } else {
                    pcb.logs.push_back(log_format(core_id, "Error: Invalid address format " + addrStr));
                    break;
                }
            }
            
            // Check address bounds
            if (address + 1 >= pcb.processMemory.size()) {
                // Memory access violation
                pcb.hasMemoryViolation = true;
                pcb.memoryViolationTime = get_timestamp();
                pcb.memoryViolationAddress = address;
                pcb.processState = State::TERMINATED;
                std::ostringstream oss;
                oss << "Memory access violation at 0x" << std::hex << std::uppercase << address;
                pcb.logs.push_back(log_format(core_id, oss.str()));
                break;
            }
            
            // Access memory through memory manager (handles page faults)
            if (globalMemory) {
                bool accessOk = globalMemory->accessMemory(pcb.process->pid, address, true);
                if (!accessOk) {
                    pcb.hasMemoryViolation = true;
                    pcb.memoryViolationTime = get_timestamp();
                    pcb.memoryViolationAddress = address;
                    pcb.processState = State::TERMINATED;
                    pcb.logs.push_back(log_format(core_id, "Memory access failed"));
                    break;
                }
            }
            
            // Get value from variable
            uint16_t value = pcb.readVariable(varName);
            
            // Write value to memory address
            if (!pcb.writeMemoryAddress(address, value)) {
                pcb.hasMemoryViolation = true;
                pcb.memoryViolationTime = get_timestamp();
                pcb.memoryViolationAddress = address;
                pcb.processState = State::TERMINATED;
                std::ostringstream oss;
                oss << "Memory write failed at 0x" << std::hex << std::uppercase << address;
                pcb.logs.push_back(log_format(core_id, oss.str()));
            }
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