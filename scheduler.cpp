#include "utils.h"
#include "process.h"
#include "globals.h"
#include <random>

void scheduler_start() {
    scheduler_running = true;

    std::random_device rd;
    std::mt19937 gen(rd());
    
    std::uniform_int_distribution<> instruction_distrib(min_ins, max_ins);
    std::uniform_int_distribution<> instruction_type_distrib(0, 5); 
    std::uniform_int_distribution<> uint16_distrib(0, 65535);
    std::uniform_int_distribution<> uint8_distrib(0, 255);

    while (scheduler_running) {
        int num_instructions = instruction_distrib(gen);
        Process* process = new Process;
        process->pid = generate_pid();
        process->name = generate_process_name();

        for (int i = 0; i < num_instructions; ++i) {
            Instruction instruction;
            instruction.type = static_cast<InstructionType>(instruction_type_distrib(gen));

            switch (instruction.type) {
                case PRINT:
                    // TODO: rand msg generation logic
                    break;
                case DECLARE:
                    instruction.arg1 = "var" + std::to_string(uint16_distrib(gen));
                    instruction.val1 = uint16_distrib(gen);
                    break;
                case ADD:
                case SUBTRACT:
                    instruction.val1 = instruction_distrib(gen);
                    instruction.val2 = instruction_distrib(gen);
                    instruction.isLiteral1 = true;
                    instruction.isLiteral2 = true;
                    break;
                case SLEEP:
                    instruction.val1 = uint8_distrib(gen);
                    break;
                case FOR_LOOP:
                    // TODO: rand instructions and repeat num
                    break;
            }

            process->instructions.push_back(instruction);
        }

        
    }
}

void scheduler_stop() {
    scheduler_running = false;
}