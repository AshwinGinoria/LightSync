#include "effect.hpp"
#include "../effect_manager.hpp"
#include "../logger.hpp"

Effect::Effect(std::string name, int interval_ms)
    : name(name), interval_ms(interval_ms), is_running(false) {
    LOGGER.info("{}: registering effect!", name);
    EffectManager::getInstance().registerEffect(this);
};

void Effect::set_color_parameter(std::array<uint8_t, 3> &param,
                                 const Parameter &new_param) {
    if (std::holds_alternative<std::array<uint8_t, 3>>(new_param.value)) {
        param = std::get<std::array<uint8_t, 3>>(new_param.value);
    } else {
        LOGGER.error("{}: Invalid type. Expected (uint8, uint8, uint8)", name);
    }
}

void Effect::set_int_parameter(int &param, const Parameter &new_param) {
    if (std::holds_alternative<int>(new_param.value)) {
        param = std::get<int>(new_param.value);
    }
}

Effect::~Effect() {
    stop();
};

// returns the name of the effect
std::string Effect::get_effect_name() {
    return name;
}

// Iteratively calls set_paramter
void Effect::set_parameters(const std::map<std::string, Parameter> &params) {
    LOGGER.info("{}: updating parameters", name);
    for (const auto &[key, value] : params) {
        LOGGER.info("{}: set_parameter {} = {}", name, key, value.to_string());
        set_parameter(key, value);
    }
}

void Effect::stop() {
    if (is_running) {
        LOGGER.info("{}: stopping effect", name);
        is_running = false;
    }
}

// Is the effect running?
bool Effect::status() {
    return is_running;
}

void Effect::start(LEDStrip &lights) {
    LOGGER.info("{}: starting effect", name);

    is_running = true;
    while (is_running) {
        animate(lights);
        std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
    }

    LOGGER.info("{}: effect stopped", name);
}
