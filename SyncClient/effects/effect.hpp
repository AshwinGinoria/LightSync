#pragma once
#include "../led_strip.hpp"
#include "../parameter.hpp"
#include <thread>
#include <chrono>
#include <variant>
#include <string>
#include <map>
#include <array>

/* ---------------- Interface ------------------ */

class Effect
{
protected:
    int interval_ms;              // interval for between 2 frames
    std::atomic<bool> is_running; // Current Status
    const std::string name;       // Effect Name

    virtual void set_parameter(const std::string &, const Parameter &) = 0; // set the value of a parameter
    void set_int_parameter(int &, const Parameter &);                       // helper function for child classes
    void set_color_parameter(std::array<uint8_t, 3> &, const Parameter &);  // helper function for child classes

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
