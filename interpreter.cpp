// interpreter.cpp
#include "interpreter.h"
#include "globals.h"
#include "utils.h"
#include <thread>
#include <chrono>

void command_interpreter_thread_func() {
    while (is_running) {
        std::string command_line;
        {
            std::unique_lock<std::mutex> lock(command_queue_mutex);
            if (!command_queue.empty()) {
                command_line = command_queue.front(); // gets the next command in the queue
                command_queue.pop(); // pops it from the queue
            }
        }

        if (!command_line.empty()) {
            std::vector<std::string> tokens = split_string(command_line); // splits the command line into tokens for commands with parameters (e.g., set_text Hello World)
                if (tokens.empty()) continue;
            std::string command = to_lowercase(tokens[0]);

            if (command == "help") {
                std::unique_lock<std::mutex> lock(prompt_mutex);
                prompt_display_buffer =
                    "Available commands:\n"
                    "help - show commands\n"
                    "start - start animation\n"
                    "stop - stop animation\n"
                    "message <text> - set marquee text\n"
                    "speed <ms> - set animation speed\n"
                    "exit - quit program";
            } else if (command == "start") {
                marquee_running = true;
                std::unique_lock<std::mutex> lock(prompt_mutex);
                prompt_display_buffer = "Marquee started.";
            } else if (command == "stop") {
                marquee_running = false;
                std::unique_lock<std::mutex> lock(prompt_mutex);
                prompt_display_buffer = "Marquee stopped.";
            } else if (command == "message") { // checks if there is a text parameter provided
                if (tokens.size() > 1) {
                    std::string new_text;
                    for (size_t i = 1; i < tokens.size(); ++i) { // loops through the tokens to reconstruct the text
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
            } else if (command == "speed") {
                if (tokens.size() > 1) { // checks if there is a speed parameter provided
                    try {
                        int speed = std::stoi(tokens[1]); // converts the speed parameter from a string to an integer
                        if (speed > 0) { // validates that the speed is a positive integer
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