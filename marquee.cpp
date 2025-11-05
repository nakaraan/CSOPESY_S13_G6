// marquee.cpp
#include "marquee.h"
#include "globals.h"
#include <thread>
#include <chrono>

void marquee_logic_thread_func(int display_width) {
    while (is_running) {
        if (marquee_running) {
            // compute wrap length
            int text_len;
            {
                std::unique_lock<std::mutex> lock(marquee_to_display_mutex);
                text_len = (int)marquee_text.size();
            }
            int cycle = std::max(text_len, display_width) + display_width;
            marquee_position = (marquee_position + 1) % cycle;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(marquee_speed));
    }
}