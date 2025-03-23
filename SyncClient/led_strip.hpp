#pragma once

#include "udp_server.hpp"
#include <array>
#include <mutex>
#include <vector>

/* ---------------- Interface ------------------ */

class LEDStrip
{
private:
    UDP_Server server;
    float scale;
    std::vector<uint8_t> byte_message;
    std::mutex ledmutex;

public:
    const int n_pixels;

    LEDStrip(int, std::string, int, float = 0.1);             // Initialize the strip
    void off();                                               // Turn off the led
    void fill(const std::array<uint8_t, 3> &);                // Fill the LED strip with a single color
    void update(const std::vector<std::array<uint8_t, 3>> &); // Update the LED strip with the given pixel colors

    void set_scale(float); // setter for scale
    float get_scale();     // getter for scale
};
