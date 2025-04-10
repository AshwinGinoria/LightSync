#include "parameter.hpp"
#include "effects/effect.hpp"

#include <map>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

class EffectManager {
  private:
    // CONST value for when no effect is active.
    static const std::string OFF;
    // registry for storing effect references mapped to effect_name
    static std::map<std::string, Effect *> registry;
    // name of current effect running
    static std::string currentEffect;
    // LED Strip object that would be passed to all effects
    static LEDStrip *lights;
    // mutex to make it thread safe
    static std::mutex effectMutex;
    // thread running the effect
    static std::thread effectThread;

    // return the effect reference if effect name found in registry
    static std::optional<Effect *> getEffectFromRegistry(const std::string &);
    // stop the currently running effect, if any.
    static void stopCurrentEffect();

  public:
    // Singleton
    static EffectManager &getInstance();
    // Default Constructor
    EffectManager();

    // setter function for lights, can only be called once
    static void setLights(LEDStrip *);
    // register a new effect
    static void registerEffect(Effect *);
    // check if the effect manager is running
    static bool isRunning();

    // -- UI Elements --
    // returns a list of all available effects
    static std::vector<std::string> getAvailableEffects();
    // fetches the parameter map for the requested effect
    static std::map<std::string, Parameter>
    getEffectParameters(const std::string &);
    // fetches the built in scaling for LED Strip
    static float getLightScale();

    // -- Actions --
    // update the built in scaling for LED Strip
    static void setLightScale(float);
    // update the parameter of the requested effect
    static bool setEffectParameters(const std::string &,
                                    const std::map<std::string, Parameter>);
    // start the effect with the given name
    static void startEffect(const std::string &);
    // stop effect_manager and turn off the lights
    static void stop();
};
