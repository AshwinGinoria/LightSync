#include "effect_manager.hpp"
#include "logger.hpp"

// Initialize Static Members
const std::string EffectManager::OFF = "OFF";
std::string EffectManager::currentEffect = EffectManager::OFF;
std::map<std::string, Effect *> EffectManager::registry;
LEDStrip *EffectManager::lights = nullptr;
std::mutex EffectManager::effectMutex;
std::thread EffectManager::effectThread;

/* ------------------ Implementation ------------------ */

EffectManager::EffectManager() {
    lights = nullptr;
}

void EffectManager::registerEffect(Effect *effect) {
    std::string effect_name = effect->get_effect_name();

    if (getEffectFromRegistry(effect_name).has_value()) {
        LOGGER.error(
            "Could not register effect, key '{}' already present in registry",
            effect_name);
        return;
    }

    registry.insert({effect_name, effect});
    LOGGER.info("Effect Registered Successfully: {}", effect_name);
}

void EffectManager::stopCurrentEffect() {
    std::lock_guard<std::mutex> lock(effectMutex);
    if (currentEffect != OFF) {
        LOGGER.info("Stopping active effect: {}", currentEffect);
        getEffectFromRegistry(currentEffect).value()->stop();

        if (effectThread.joinable()) effectThread.join();

        currentEffect = OFF;
    }
}

void EffectManager::setLights(LEDStrip *lights) {
    LOGGER.info("Setting lights for EffectManager");

    if (EffectManager::lights == nullptr) {
        EffectManager::lights = lights;
        LOGGER.info("Lights set successfully!");
    } else
        LOGGER.error("Lights can only be set once!");
}

std::vector<std::string> EffectManager::getAvailableEffects() {
    std::vector<std::string> names;
    for (const auto &[name, _] : registry) {
        names.push_back(name);
    }
    return names;
}

std::map<std::string, Parameter>
EffectManager::getEffectParameters(const std::string &effect_name) {
    std::optional<Effect *> some_new_effect =
        getEffectFromRegistry(effect_name);

    if (!some_new_effect.has_value()) return {};

    return some_new_effect.value()->get_parameters();
}

bool EffectManager::setEffectParameters(
    const std::string &effect_name,
    const std::map<std::string, Parameter> parameters) {
    LOGGER.info("Setting parameters for effect: {}", effect_name);
    std::optional<Effect *> some_new_effect =
        getEffectFromRegistry(effect_name);

    if (!some_new_effect.has_value()) return false;

    some_new_effect.value()->set_parameters(parameters);
    return true;
}

void EffectManager::startEffect(const std::string &effect_name) {
    LOGGER.info("Starting effect: {}", effect_name);
    if (currentEffect == effect_name) {
        LOGGER.info("{} is already running!", effect_name);
        return;
    }

    std::optional<Effect *> some_new_effect =
        getEffectFromRegistry(effect_name);

    if (!some_new_effect.has_value()) return;

    Effect *new_effect = some_new_effect.value();

    stopCurrentEffect();

    std::lock_guard<std::mutex> lock(effectMutex);

    LOGGER.info("Starting effect: {}", effect_name);

    currentEffect = effect_name;
    effectThread = std::thread([new_effect]() { new_effect->start(*lights); });
    effectThread.detach();
}

void EffectManager::stop() {
    stopCurrentEffect();
    lights->off();
}

std::optional<Effect *>
EffectManager::getEffectFromRegistry(const std::string &effect_name) {
    if (registry.find(effect_name) == registry.end()) {
        LOGGER.debug("Effect {} not found!:", effect_name);
        return std::nullopt;
    }

    return registry[effect_name];
}

EffectManager &EffectManager::getInstance() {
    static EffectManager instance;
    return instance;
}

float EffectManager::getLightScale() {
    return lights->getScale();
}

void EffectManager::setLightScale(float scale) {
    lights->setScale(scale);
}

bool EffectManager::isRunning() {
    return currentEffect != OFF;
}