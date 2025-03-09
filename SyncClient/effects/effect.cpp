#pragma once
#include "../led_strip.cpp"
#include "../logger.cpp"
#include <thread>
#include <chrono>

class Effect
{
protected:
    LEDStrip *lights;
    Logger *logger;
    int interval_ms;
    bool is_running;

public:
    Effect(LEDStrip *lights, int interval_ms = 60000)
    {
        this->logger = Logger::getInstance();
        this->lights = lights;
        this->interval_ms = interval_ms;
        this->is_running = false;
    }

    // this->lights->fill({0, 0, 0});
    void animate() {};

    void stop()
    {
        logger->info("Stopping effect!");
        this->is_running = false;
    }

    void run()
    {
        logger->info("Running effect!");

        this->is_running = true;
        while (this->is_running)
        {
            this->animate();
            std::this_thread::sleep_for(std::chrono::milliseconds(this->interval_ms));
        }

        logger->info("Effect stopped!");
    }
};
