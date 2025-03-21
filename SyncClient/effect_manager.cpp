#include "effects/effect.cpp"
#include <mutex>
#include <map>
#include <vector>
#include <thread>
#include "logger.cpp"
#include <optional>

/* ------------------ Interface ------------------ */

class EffectManager
{
private:
    static const std::string OFF;                   // CONST value for when no effect is active.
    static Logger &logger;                          // Logger

    static std::map<std::string, std::reference_wrapper<Effect>> registry; // registry for storing effect references mapped to effect_name
    static std::string current_effect;                                     // name of current effect running
    static std::optional<LEDStrip &> lights;                               // LED Strip object that would be passed to all effects

    static std::mutex effect_mutex;   // mutex to make it thread safe
    static std::thread effect_thread; // thread running the effect

    static std::optional<Effect &> get_effect_from_registry(const std::string &); // return the effect reference if effect name found in registry
    static void stop_current_effect();                                            // stop the currently running effect, if any.

public:
    EffectManager() {};
    static void set_lights(LEDStrip &);                                                             // setter function for lights, can only be called once
    static void register_effect(std::reference_wrapper<Effect>);                                    // register a new effect
    static std::vector<std::string> get_available_effects();                                        // returns a list of all available effects
    static std::map<std::string, Parameter> get_effect_parameters(const std::string &);             // fetches the parameter map for the requested effect
    static bool set_effect_parameters(const std::string &, const std::map<std::string, Parameter>); // update the parameter of the requested effect
    static void start_effect(const std::string &);                                                  // start the effect with the given name
    static void stop();                                                                             // stop effect_manager and turn off the lights
    static EffectManager &get_instance();
};

// Initialize Static Members
const std::string EffectManager::OFF = "OFF";
std::optional<LEDStrip &> EffectManager::lights = std::optional<LEDStrip &>();
std::string EffectManager::current_effect = EffectManager::OFF;
Logger &EffectManager::logger = Logger::getInstance();

/* ------------------ Implementation ------------------ */

void EffectManager::register_effect(std::reference_wrapper<Effect> effect)
{
    std::string effect_name = effect.get().get_effect_name();
    if (get_effect_from_registry(effect_name))
    {
        logger.error("Could not register effect, key '{}' already present in registry", effect_name);
    }

    registry.insert({effect_name, effect});
}

void EffectManager::stop_current_effect()
{
    std::lock_guard<std::mutex> lock(effect_mutex);
    if (current_effect != OFF)
    {
        logger.info("Stopping active effect: {}", current_effect);
        get_effect_from_registry(current_effect).value().stop();

        if (effect_thread.joinable())
            effect_thread.join();

        current_effect = OFF;
    }
}

void EffectManager::set_lights(LEDStrip &lights)
{
    if (EffectManager::lights.has_value())
        logger.error("Lights can only be set once!");
    else
        EffectManager::lights.emplace(lights);
}

std::vector<std::string> EffectManager::get_available_effects()
{
    std::vector<std::string> names;
    for (const auto &[name, _] : registry)
    {
        names.push_back(name);
    }
    return names;
}

std::map<std::string, Parameter> EffectManager::get_effect_parameters(const std::string &effect_name)
{
    std::optional<Effect &> some_new_effect = get_effect_from_registry(effect_name);

    if (!some_new_effect.has_value())
        return {};

    return some_new_effect.value().get_parameters();
}

bool EffectManager::set_effect_parameters(const std::string &effect_name, const std::map<std::string, Parameter> parameters)
{
    std::optional<Effect &> some_new_effect = get_effect_from_registry(effect_name);

    if (!some_new_effect.has_value())
        return false;

    some_new_effect.value().set_parameters(parameters);
    return true;
}

void EffectManager::start_effect(const std::string &effect_name)
{
    if (current_effect == effect_name)
    {
        logger.info("{} is already running!", effect_name);
        return;
    }

    std::optional<Effect &> some_new_effect = get_effect_from_registry(effect_name);

    if (!some_new_effect.has_value())
        return;

    Effect &new_effect = some_new_effect.value();

    stop_current_effect();

    std::lock_guard<std::mutex> lock(effect_mutex);

    logger.info("Starting effect: {}", effect_name);

    current_effect = effect_name;
    effect_thread = std::thread(&Effect::start, new_effect, std::ref(*lights));
    effect_thread.detach();
}

void EffectManager::stop()
{
    stop_current_effect();
    lights->off();
}

std::optional<Effect &> EffectManager::get_effect_from_registry(const std::string &effect_name)
{
    if (registry.contains(effect_name))
    {
        logger.error("Effect {} not found!:", effect_name);
        return std::optional<Effect &>();
    }

    return registry[effect_name];
}

EffectManager& EffectManager::get_instance()
{
    static EffectManager instance;
    return instance;
}
