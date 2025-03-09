#include "effect.cpp"
#include <array>
#include "../led_strip.cpp"

class Blink : public Effect
{
    std::array<uint8_t, 3> color1;
    std::array<uint8_t, 3> color2;
    bool state;
    LEDStrip *lights;

public:
    Blink(
        LEDStrip *lights,
        std::array<uint8_t, 3> color1 = {0, 255, 0},
        std::array<uint8_t, 3> color2 = {0, 0, 0}
    ) : Effect(lights)
    {
        this->lights = lights;
        this->color1 = color1;
        this->color2 = color2;
        this->state = false;
    }

    void animate()
    {
        if (this->state)
        {
            this->lights->fill(this->color1);
            this->state = false;
        }
        else
        {
            this->lights->fill(this->color2);
            this->state = true;
        }
    }
};
