#include <cmath>
#include "led_strip.hpp"
#include "logger.hpp"

LEDStrip::LEDStrip(int length, std::string ip_address, int port, float scale) : n_pixels(length), scale(scale), server(UDP_Server(port, ip_address))
{
    // Initialize the byte message with the number of LEDs
    byte_message.assign(this->n_pixels * 3, 0);

    LOGGER.info("LEDStrip initialized with {} pixels, IP: {}, Port: {}, Scale: {}", length, ip_address, port, scale);
}

void LEDStrip::off()
{
    fill({0, 0, 0});
}

// Fill the LED strip with a single color
void LEDStrip::fill(const std::array<uint8_t, 3> &color)
{
    LOGGER.debug("Filling LED strip with color: [{}, {}, {}]", color[0], color[1], color[2]);
    std::vector<std::array<uint8_t, 3>> leds(n_pixels, color);
    update(leds);
}

// Update the LED strip with the given pixel colors
void LEDStrip::update(const std::vector<std::array<uint8_t, 3>> &pixels)
{
    std::lock_guard<std::mutex> lock(ledmutex);

    LOGGER.debug("Updating LED strip with new pixel data");
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

float LEDStrip::get_scale()
{
    return scale;
}

void LEDStrip::set_scale(float value)
{
    std::lock_guard<std::mutex> lock(ledmutex);
    scale = value;
}
