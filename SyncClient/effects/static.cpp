#include "../led_strip.cpp"
#include "effect.cpp"
#include "../logger.cpp"
#include <array>

class Static: public Effect {
    std::array<uint8_t, 3> color;

public:
    Static(LEDStrip *lights, std::array<uint8_t, 3> color = {0, 255, 255}): Effect(lights) {
        this->color = color;
    }

    void animate() {
        this->lights->fill(this->color);
    }
};