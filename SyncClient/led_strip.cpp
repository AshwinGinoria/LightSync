#pragma once
#include "logger.cpp"
#include "udp_server.cpp"
#include <array>
#include <vector>
#include <cmath>

/* ---------------- Interface ------------------ */

class LEDStrip
{
private:
    UDP_Server server;
    Logger &logger;
    float scale;
    std::vector<uint8_t> byte_message;

public:
    const int n_pixels;

    LEDStrip(int, std::string, int, float = 0.1);             // Initialize the strip
    void off();                                               // Turn off the led
    void fill(const std::array<uint8_t, 3> &);                // Fill the LED strip with a single color
    void update(const std::vector<std::array<uint8_t, 3>> &); // Update the LED strip with the given pixel colors
};

/* ------------------ Implementation ------------------ */

LEDStrip::LEDStrip(int length, std::string ip_address, int port, float scale = 0.1) : n_pixels(length), scale(scale), logger(Logger::getInstance()), server(UDP_Server(port, ip_address))
{
    // Initialize the byte message with the number of LEDs
    byte_message.assign(this->n_pixels * 3, 0);

    logger.info("LEDStrip initialized with {} pixels, IP: {}, Port: {}, Scale: {}", length, ip_address, port, scale);
}

void LEDStrip::off()
{
    fill({0, 0, 0});
}

// Fill the LED strip with a single color
void LEDStrip::fill(const std::array<uint8_t, 3> &color)
{
    logger.debug("Filling LED strip with color: [{}, {}, {}]", color[0], color[1], color[2]);
    std::vector<std::array<uint8_t, 3>> leds(n_pixels, color);
    update(leds);
}

// Update the LED strip with the given pixel colors
void LEDStrip::update(const std::vector<std::array<uint8_t, 3>> &pixels)
{
    logger.debug("Updating LED strip with new pixel data");
    int i = 0;
    for (const auto &pixel : pixels)
    {
        for (uint8_t led : pixel)
        {
            byte_message[i++] = static_cast<uint8_t>(std::round(led * scale));
        }
    }

    // Send the message
    server.send_message(&byte_message);
}
