#include "../logger.hpp"
#include "effect.hpp"
#include <array>

class Blink : public Effect
{
    std::array<uint8_t, 3> color1;
    std::array<uint8_t, 3> color2;
    bool state;

    void set_parameter(const std::string &key, const Parameter &value)
    {
        if (key == "ColorA")
            set_color_parameter(color1, value);
        else if (key == "ColorB")
            set_color_parameter(color2, value);
        else if (key == "Interval")
            set_int_parameter(interval_ms, value);
        else
            LOGGER.error("{}: Undefined Paramter {}", name, key);
    }

public:
    Blink(int interval_ms = 10000,
          std::array<uint8_t, 3> color1 = {0, 255, 0},
          std::array<uint8_t, 3> color2 = {0, 0, 0}) : Effect("Blink", interval_ms), color1(color1), color2(color2), state(true) {};

    std::map<std::string, Parameter> get_parameters()
    {
        return {
            {"ColorA", color1},
            {"ColorB", color2},
            {"Interval", interval_ms}};
    }

    void animate(LEDStrip &lights) override
    {
        if (state)
            lights.fill(color1);
        else
            lights.fill(color2);

        // flip the state
        state = !state;
    }
};

static Blink blink_effect;