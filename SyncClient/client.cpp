#include "led_strip.cpp"
#include "logger.cpp"
#include "effect_manager.cpp"

#include <string>
#include <csignal>
#include <memory>

std::string SERVER_IP = "192.168.0.244";
std::unique_ptr<LEDStrip> lights;

void signal_handler(int signal) {
    if (signal == SIGINT) {
        Logger::getInstance()->info("Caught SIGINT, cleaning up...");
        if (lights) {
            lights->fill({0, 0, 0});
        }
        effect.reset(); // Ensure effect is cleaned up
        lights.reset(); // Ensure lights are cleaned up
        exit(0);
    }
}

void run_lights(Mode mode) {
    lights = std::make_unique<LEDStrip>(288, SERVER_IP, 5005, 0.1);

    switch (mode)
    {
    case STATIC:
        effect = std::make_unique<Static>(lights.get(), std::array<uint8_t, 3>{39, 15, 0});
        break;
    case BLINK:
        effect = std::make_unique<Blink>(lights.get(), 1000);
        break;
    case REPLICATE:
        effect = std::make_unique<Replicate>(lights.get());
        break;
    default:
        break;
    }

    effect->run();
}

int main() {
    // Register signal handler for SIGINT
    std::signal(SIGINT, signal_handler);

    run_lights(REPLICATE);
    return 0;
}