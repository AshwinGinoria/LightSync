#pragma once
#include "../led_strip.hpp"
#include "../parameter.hpp"
#include <array>
#include <chrono>
#include <map>
#include <string>
#include <thread>
#include <variant>

/* ---------------- Interface ------------------ */

class Effect {
  protected:
    int interval_ms;              // sleep interval between 2 frames
    std::atomic<bool> is_running; // current status
    const std::string name;       // effect name

    // set the value of a parameter
    virtual void set_parameter(const std::string &, const Parameter &) = 0;

    // helper functions for child classes
    void set_int_parameter(int &, const Parameter &);
    void set_color_parameter(std::array<uint8_t, 3> &, const Parameter &);

  public:
    // Default Constructor
    Effect(std::string name, int interval_ms = 60000);
    // Virtual Destructor
    virtual ~Effect();

    // returns the map of configurable parameters
    virtual std::map<std::string, Parameter> get_parameters() = 0;
    // core logic for the effect, should only use the class paramters
    virtual void animate(LEDStrip &) = 0;
    void start(LEDStrip &); // start running the effect
    void stop();            // stop running the effect
    bool status();          // returns is_running

    // returns the effect name
    std::string get_effect_name();
    // iteratively calls set_parameter
    void set_parameters(const std::map<std::string, Parameter> &);
};
