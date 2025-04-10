#include "../logger.hpp"
#include "effect.hpp"
#include <array>
#include <variant>

class Static : public Effect
{
    std::array<uint8_t, 3> color;

    void set_parameter(const std::string &key, const Parameter &value)
    {
        if (key == "Color")
            set_color_parameter(color, value);
        else if (key == "Interval")
            set_int_parameter(interval_ms, value);
        else
            LOGGER.error("{}: Undefined Paramter {}", name, key);
    }

public:
    Static(std::array<uint8_t, 3> color = {0, 255, 255}) : Effect("Static"), color(color) {}

    std::map<std::string, Parameter> get_parameters()
    {
        return {
            {"Color", color},
            {"Interval", interval_ms}};
    }

    void animate(LEDStrip &lights) override
    {
        lights.fill(this->color);
        is_running = false;
    }
};

static Static static_effect;
