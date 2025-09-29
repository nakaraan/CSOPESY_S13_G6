#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>
#include <iomanip>
#include <queue>

// --- Shared State and Thread Control ---
// Global flag to signal all threads to exit.
std::atomic<bool> is_running{true};

// The command interpreter and display thread share this variable.
std::string prompt_display_buffer = "";
std::mutex prompt_mutex;

// Shared state for the keyboard handler and command interpreter.
std::queue<std::string> command_queue;
std::mutex command_queue_mutex;

// The marquee logic thread and display thread share this variable.
std::string marquee_display_buffer = "";
std::mutex marquee_to_display_mutex;

// --- Utility Function ---
// Moves the cursor to a specific (x, y) coordinate on the console.
void gotoxy(int x, int y) {
    std::cout << "\033[" << y << ";" << x << "H";
}

// The state variables for the marquee
std::atomic<bool> marquee_running{false};
std::string marquee_text = "Welcome to CSOPESY!";
std::atomic<int> marquee_speed{300};
std::atomic<int> marquee_position{0};

// --- Thread Functions ---
void keyboard_handler_thread_func() {
    std::string command_line;
    while (is_running) {
        std::getline(std::cin, command_line);
        if (!command_line.empty()) {
            std::unique_lock<std::mutex> lock(command_queue_mutex);
            command_queue.push(command_line);
            lock.unlock();
        }
    }
}

void marquee_logic_thread_func(int display_width) {
    while (is_running) {
        // Marquee logic goes here...
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

void display_thread_func() {
    const int refresh_rate_ms = 50;
    while (is_running) {
        // Display logic goes here...
        std::this_thread::sleep_for(std::chrono::milliseconds(refresh_rate_ms));
    }
}
std::vector<std::string> split_string(const std::string& str) { // splits the string into individual tokens
    std::istringstream iss(str);
    std::vector<std::string> tokens;
    std::string token;
    while (iss >> token) { // loops through the input string stream to extract words
        tokens.push_back(token); // adds the extracted word to the end of the vector
    }
    return tokens; // returns a vector of strings where each token becomes an element
}

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
                prompt_display_buffer = "Available commands: \n\n" + "help - displays the list of available commands\n" +
                    "start_marquee - starts the marquee animation\n" + "stop_marquee - stops the marquee animation\n" +
                    "set_text <text> - accepts a text input and displays it as a marquee\n" + "set_speed <speed> - sets the marquee animation refresh in milliseconds\n" +
                    "exit - terminates the console";
            } else if (command == "start_marquee") {
                marquee_running = true;
                std::unique_lock<std::mutex> lock(prompt_mutex);
				prompt_display_buffer = "Marquee started.";
            } else if (command == "stop_marquee") {
                marquee_running = false;
				std::unique_lock<std::mutex> lock(prompt_mutex);
				prompt_display_buffer = "Marquee stopped.";
            }
            else if (command == "set_text") {
				if (tokens.size() > 1) { // checks if there is a text parameter provided
                    std::string new_text;
                    for (size_t i = 1; i < tokens.size(); ++i) { // loops through the tokens to reconstruct the text
                        if (i > 1) new_text += " ";
                        new_text += tokens[i];
                    }
                    marquee_text = new_text;
                    marquee_position = 0;
                    std::unique_lock<std::mutex> lock(prompt_mutex);
                    prompt_display_buffer = "Marquee text updated to " + new_text;
                } else {
                    std::unique_lock<std::mutex> lock(prompt_mutex);
					prompt_display_buffer = "No text parameter provided.";
				}
            }
            else if (command == "set_speed") {
                if (tokens.size() > 1) { // checks if there is a speed parameter provided
                    try {
						int speed = std::stoi(tokens[1]); // converts the speed parameter from a string to an integer
						if (speed > 0) { // validates that the speed is a positive integer
                            marquee_speed = speed;
                            std::unique_lock<std::mutex> lock(prompt_mutex);
                            prompt_display_buffer = "Marquee speed set to " + std::to_string(speed) + "ms";
                        }
                        else {
                            std::unique_lock<std::mutex> lock(prompt_mutex);
                            prompt_display_buffer = "Marquee speed must be a positive value.";
                        }
                    } catch (const std::invalid_argument&) {
                        std::unique_lock<std::mutex> lock(prompt_mutex);
                        prompt_display_buffer = "Invalid speed value.";
                    }
                }
                else {
                    std::unique_lock<std::mutex> lock(prompt_mutex);
                    prompt_display_buffer = "No speed parameter provided.";
                }
            } else if (command == "exit") {
                is_running = false;
                std::unique_lock<std::mutex> lock(prompt_mutex);
                prompt_display_buffer = "Exiting console.";
            }
            else {
                std::unique_lock<std::mutex> lock(prompt_mutex);
                prompt_display_buffer = "Unknown command. Type 'help' for available commands.";
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

// --- Main Function (Command Interpreter Thread) ---
int main() {
    // Start the three worker threads.
    std::thread marquee_logic_thread(marquee_logic_thread_func, 40);
    std::thread display_thread(display_thread_func);
    std::thread keyboard_handler_thread(keyboard_handler_thread_func);
	std::thread command_interpreter_thread(command_interpreter_thread_func);

    // Join threads to ensure they finish cleanly.
    if (marquee_logic_thread.joinable()) {
        marquee_logic_thread.join();
    }
    if (display_thread.joinable()) {
        display_thread.join();
    }
    if (keyboard_handler_thread.joinable()) {
        keyboard_handler_thread.join();
    }

    return 0;
}
