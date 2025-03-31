#pragma once

#include "udp_server.hpp"
#include <array>
#include <mutex>
#include <vector>

/* ---------------- Interface ------------------ */

class LEDStrip {
  private:
    UDP_Server server;
    float scale;
    std::vector<uint8_t> byteMessage;
    std::mutex ledMutex;

    // Scale the pixel color based on the scale factor
    std::array<uint8_t, 3> scalePixel(std::array<uint8_t, 3>);

  public:
    const int nPixels;

    // Initialize the strip
    LEDStrip(int, std::string, int, float = 0.1);
    // Turn off the led
    void off();
    // Fill the LED strip with a single color
    void fill(const std::array<uint8_t, 3> &);
    // Update the LED strip with the given pixel colors
    void update(const std::vector<std::array<uint8_t, 3>> &);

    // setter for scale
    void setScale(float);
    // getter for scale
    float getScale();
};
