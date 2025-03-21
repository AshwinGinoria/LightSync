#pragma once
#include "../led_strip.cpp"
#include "../logger.cpp"
#include "../effect_manager.cpp"
#include <thread>
#include <chrono>
#include <variant>
#include <string>
#include <map>
#include <array>

using Parameter = std::variant<int, float, bool, std::string, std::array<uint8_t, 3>>;

/* ---------------- Interface ------------------ */

class Effect
{
protected:
    Logger &logger;               // logger helper
    int interval_ms;              // interval for between 2 frames
    std::atomic<bool> is_running; // Current Status
    const std::string name;       // Effect Name

    virtual void set_parameter(const std::string &, Parameter) = 0;                     // set the value of a parameter
    void set_color_parameter(const std::string&, std::array<uint8_t, 3>&, Parameter);   // helper function for child classes
    void set_interval_ms(Parameter);                                                    // helper function for child classes

public:
    Effect(std::string name, int interval_ms = 60000);             // Constructor
    virtual ~Effect();                                             // Virtual Destructor
    virtual std::map<std::string, Parameter> get_parameters() = 0; // returns the map of configurable parameters
    virtual void animate(LEDStrip &) = 0;                          // core logic for the effect, should only use the class paramters
    std::string get_effect_name();                                 // returns the effect name
    void set_parameters(const std::map<std::string, Parameter> &); // iteratively calls set_parameter
    void stop();                                                   // stop running the effect
    bool status();                                                 // returns is_running
    void start(LEDStrip &);                                        // start running the effect
};

/* ---------------- Implementation ------------------ */

Effect::Effect(std::string name, int interval_ms) : name(name), interval_ms(interval_ms), is_running(false), logger(Logger::getInstance())
{
    EffectManager::register_effect(std::ref<Effect>(*this));
}

void Effect::set_color_parameter(const std::string &key, std::array<uint8_t, 3>& param, Parameter value)
{
    if (std::holds_alternative<std::array<uint8_t, 3>>(value))
    {
        param = std::get<std::array<uint8_t, 3>>(value);
    }
    else
    {
        logger.info("Invalid type for '{}'. Expected {uint8, uint8, uint8}", key);
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
    logger.info("Stopping effect!");
    is_running = false;
}

// Is the effect running?
bool Effect::status()
{
    return is_running;
}

void Effect::start(LEDStrip &lights)
{
    logger.info("Starting effect!");

    is_running = true;
    while (is_running)
    {
        animate(lights);
        std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
    }

    logger.info("Effect stopped!");
}
