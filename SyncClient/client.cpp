#include <QApplication>
#include "effect_manager.hpp"
#include "effect_manager_ui.hpp"
#include "led_strip.hpp"
#include "logger.hpp"
#include <csignal>
#include <cstdlib>

std::string SERVER_IP = "192.168.0.244";
int PORT = 5005;
float SCALE = 0.2;
bool DEBUG_MODE = false;

void signalHandler(int signum)
{
    // Cleanup or custom logic here
    EffectManager::get_instance().stop();
}

void run_debug()
{
    LOGGER.debug("Debugging LED Strip Client");
    // Add any debugging code here
    LEDStrip lights(288, SERVER_IP, PORT, SCALE);
    EffectManager::get_instance().set_lights(&lights);
    EffectManager::get_instance().start_effect("Replicate");

    // Register signal handler
    std::signal(SIGINT, signalHandler);

    // Simulate long-running process
    while (EffectManager::get_instance().is_running())
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

int main(int argc, char *argv[])
{
    if (DEBUG_MODE)
    {
        run_debug();
        return 0;
    }

    LOGGER.debug("Starting LED Strip Client");
    LEDStrip lights(288, SERVER_IP, PORT, SCALE);
    LOGGER.debug("LED Strip Client initialized with IP: {}, Port: {}, Scale: {}", SERVER_IP, PORT, SCALE);

    EffectManager::get_instance().set_lights(&lights);

    QApplication app(argc, argv);
    EffectManagerUI window;
    window.show();
    return app.exec();
}