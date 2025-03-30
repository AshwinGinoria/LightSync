#include "effect_manager.hpp"
#include "logger.hpp"

// Initialize Static Members
const std::string EffectManager::OFF = "OFF";
std::string EffectManager::current_effect = EffectManager::OFF;
std::map<std::string, Effect *> EffectManager::registry;
LEDStrip *EffectManager::lights = nullptr;
std::mutex EffectManager::effect_mutex;
std::thread EffectManager::effect_thread;

/* ------------------ Implementation ------------------ */

EffectManager::EffectManager()
{
    lights = nullptr;
}

void EffectManager::register_effect(Effect *effect)
{
    std::string effect_name = effect->get_effect_name();

    if (get_effect_from_registry(effect_name).has_value())
    {
        LOGGER.error("Could not register effect, key '{}' already present in registry", effect_name);
        return;
    }

    registry.insert({effect_name, effect});
    LOGGER.info("Effect Registered Successfully: {}", effect_name);
}

void EffectManager::stop_current_effect()
{
    std::lock_guard<std::mutex> lock(effect_mutex);
    if (current_effect != OFF)
    {
        LOGGER.info("Stopping active effect: {}", current_effect);
        get_effect_from_registry(current_effect).value()->stop();

        if (effect_thread.joinable())
            effect_thread.join();

        current_effect = OFF;
    }
}

void EffectManager::set_lights(LEDStrip *lights)
{
    LOGGER.info("Setting lights for EffectManager");

    if (EffectManager::lights == nullptr)
    {
        EffectManager::lights = lights;
        LOGGER.info("Lights set successfully!");
    }
    else
        LOGGER.error("Lights can only be set once!");
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
    std::optional<Effect *> some_new_effect = get_effect_from_registry(effect_name);

    if (!some_new_effect.has_value())
        return {};

    return some_new_effect.value()->get_parameters();
}

bool EffectManager::set_effect_parameters(const std::string &effect_name, const std::map<std::string, Parameter> parameters)
{
    LOGGER.info("Setting parameters for effect: {}", effect_name);
    std::optional<Effect *> some_new_effect = get_effect_from_registry(effect_name);

    if (!some_new_effect.has_value())
        return false;

    some_new_effect.value()->set_parameters(parameters);
    return true;
}

void EffectManager::start_effect(const std::string &effect_name)
{
    LOGGER.info("Starting effect: {}", effect_name);
    if (current_effect == effect_name)
    {
        LOGGER.info("{} is already running!", effect_name);
        return;
    }

    std::optional<Effect *> some_new_effect = get_effect_from_registry(effect_name);

    if (!some_new_effect.has_value())
        return;

    Effect *new_effect = some_new_effect.value();

    stop_current_effect();

    std::lock_guard<std::mutex> lock(effect_mutex);

    LOGGER.info("Starting effect: {}", effect_name);

    current_effect = effect_name;
    effect_thread = std::thread([new_effect]()
                                { new_effect->start(*lights); });
    effect_thread.detach();
}

void EffectManager::stop()
{
    stop_current_effect();
    lights->off();
}

std::optional<Effect *> EffectManager::get_effect_from_registry(const std::string &effect_name)
{
    if (registry.find(effect_name) == registry.end())
    {
        LOGGER.debug("Effect {} not found!:", effect_name);
        return std::nullopt;
    }

    return registry[effect_name];
}

EffectManager &EffectManager::get_instance()
{
    static EffectManager instance;
    return instance;
}

float EffectManager::get_light_scale()
{
    return lights->get_scale();
}

void EffectManager::set_light_scale(float scale)
{
    lights->set_scale(scale);
}

bool EffectManager::is_running()
{
    return current_effect != OFF;
}