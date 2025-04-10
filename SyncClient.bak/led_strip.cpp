#include "logger.hpp"
#include "led_strip.hpp"
#include <cmath>

LEDStrip::LEDStrip(int length, std::string ip_address, int port, float scale)
    : nPixels(length), scale(scale), server(UDP_Server(port, ip_address)) {
    LOGGER.debug(
        "Initializing LEDStrip with length: {}, IP: {}, Port: {}, Scale: {}",
        length, ip_address, port, scale);
    // Initialize the byte message with the number of LEDs
    byteMessage.assign(this->nPixels * 3, 0);

    LOGGER.info(
        "LEDStrip initialized with {} pixels, IP: {}, Port: {}, Scale: {}",
        length, ip_address, port, scale);
}

void LEDStrip::off() {
    fill({0, 0, 0});
}

// Fill the LED strip with a single color
void LEDStrip::fill(const std::array<uint8_t, 3> &color) {
    LOGGER.info("Filling LED strip with color: [{}, {}, {}]", int(color[0]),
                int(color[1]), int(color[2]));
    std::vector<std::array<uint8_t, 3>> leds(nPixels, color);
    update(leds);
}

// Update the LED strip with the given pixel colors
void LEDStrip::update(const std::vector<std::array<uint8_t, 3>> &pixels) {
    std::lock_guard<std::mutex> lock(ledMutex);

    LOGGER.debug("Updating LED strip with new pixel data");

    int i = 0;
    for (const auto &pixel : pixels) {
        for (uint8_t led : scalePixel(pixel)) {
            byteMessage[i++] = led;
        }
    }

    // Send the message
    server.send_message(&byteMessage);
}

std::array<uint8_t, 3> LEDStrip::scalePixel(std::array<uint8_t, 3> pixel) {
    std::array<uint8_t, 3> scaled_pixel;
    scaled_pixel[0] = std::round(pixel[0] * scale);
    scaled_pixel[1] = std::round(pixel[1] * scale);
    scaled_pixel[2] = std::round(pixel[2] * scale);

    return scaled_pixel;
}

float LEDStrip::getScale() {
    return scale;
}

void LEDStrip::setScale(float value) {
    std::lock_guard<std::mutex> lock(ledMutex);
    scale = value;
}
