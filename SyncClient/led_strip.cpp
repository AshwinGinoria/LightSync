#pragma once
#include "logger.cpp"
#include "udp_server.cpp"
#include <array>
#include <vector>
#include <cmath>

class LEDStrip
{
private:
    UDP_Server *server;
    Logger *logger;
    int n_pixels;
    float scale;
    std::vector<uint8_t> byte_message;

public:
    LEDStrip(int length, std::string ip_address, int port, float scale = 0.3)
    {
        this->logger = Logger::getInstance();
        this->n_pixels = length;
        this->scale = scale;

        // Create the server
        this->server = new UDP_Server(port, ip_address);

        // Initialize the byte message with the number of LEDs
        byte_message.assign(this->n_pixels * 3, 0);

        logger->info("LEDStrip initialized with {} pixels, IP: {}, Port: {}, Scale: {}", length, ip_address, port, scale);
    }

    // Fill the LED strip with a single color
    void fill(const std::array<uint8_t, 3> &color)
    {
        logger->debug("Filling LED strip with color: [{}, {}, {}]", color[0], color[1], color[2]);
        std::vector<std::array<uint8_t, 3>> leds(this->n_pixels, color);
        this->update(leds);
    }

    // Update the LED strip with the given pixel colors
    void update(const std::vector<std::array<uint8_t, 3>> &pixels)
    {
        logger->debug("Updating LED strip with new pixel data");
        int i = 0;
        for (const auto &pixel : pixels)
        {
            for (uint8_t led : pixel)
            {
                byte_message[i++] = static_cast<uint8_t>(std::round(led * this->scale));
            }
        }

        // Send the message
        this->server->send_message(&byte_message);
    }

    // Destructor to clean up resources
    ~LEDStrip()
    {
        logger->info("Destroying LEDStrip");
        delete server;
    }
};
