#include "effect.hpp"
#include "../logger.hpp"
#include "../effect_manager.hpp"

/* ---------------- Implementation ------------------ */

Effect::Effect(std::string name, int interval_ms) : name(name), interval_ms(interval_ms), is_running(false)
{
    LOGGER.debug("Registering effect {}", name);
    EffectManager::get_instance().register_effect(this);
};

void Effect::set_color_parameter(std::array<uint8_t, 3>& param, Parameter value)
{
    if (std::holds_alternative<std::array<uint8_t, 3>>(value))
    {
        param = std::get<std::array<uint8_t, 3>>(value);
    }
    else
    {
        LOGGER.error("Invalid type. Expected (uint8, uint8, uint8)");
    }
}

void Effect::set_interval_ms(Parameter value)
{
    if (std::holds_alternative<int>(value))
    {
        interval_ms = std::get<int>(value);
    }
}

Effect::~Effect()
{
    stop();
};

// returns the name of the effect
std::string Effect::get_effect_name() { return name; }

// Iteratively calls set_paramter
void Effect::set_parameters(const std::map<std::string, Parameter> &params)
{
    for (const auto &[key, value] : params)
    {
        set_parameter(key, value);
    }
}

void Effect::stop()
{
    if (is_running) {
        LOGGER.info("Stopping effect {}!", name);
        is_running = false;
    }
}

// Is the effect running?
bool Effect::status()
{
    return is_running;
}

void Effect::start(LEDStrip &lights)
{
    LOGGER.info("Starting effect!");

    is_running = true;
    while (is_running)
    {
        animate(lights);
        std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
    }

    LOGGER.info("Effect stopped!");
}
