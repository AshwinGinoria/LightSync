#include "effects/effect.hpp"
#include <mutex>
#include <map>
#include <vector>
#include <thread>
#include "logger.hpp"
#include <optional>
#include "parameter.hpp"

class EffectManager
{
private:
    static const std::string OFF;                    // CONST value for when no effect is active.
    static std::map<std::string, Effect *> registry; // registry for storing effect references mapped to effect_name
    static std::string current_effect;               // name of current effect running
    static LEDStrip *lights;                         // LED Strip object that would be passed to all effects
    static std::mutex effect_mutex;                  // mutex to make it thread safe
    static std::thread effect_thread;                // thread running the effect

    static std::optional<Effect *> get_effect_from_registry(const std::string &); // return the effect reference if effect name found in registry
    static void stop_current_effect();                                            // stop the currently running effect, if any.

public:
    EffectManager();                       // Default Constructor
    static void set_lights(LEDStrip *);    // setter function for lights, can only be called once
    static void register_effect(Effect *); // register a new effect
    static bool is_running();              // check if the effect manager is running
    static EffectManager &get_instance();

    // UI Elements
    static std::vector<std::string> get_available_effects();                            // returns a list of all available effects
    static std::map<std::string, Parameter> get_effect_parameters(const std::string &); // fetches the parameter map for the requested effect
    static float get_light_scale();                                                     // fetches the built in scaling for LED Strip

    // Actions
    static void set_light_scale(float);                                                             // update the built in scaling for LED Strip
    static bool set_effect_parameters(const std::string &, const std::map<std::string, Parameter>); // update the parameter of the requested effect
    static void start_effect(const std::string &);                                                  // start the effect with the given name
    static void stop();                                                                             // stop effect_manager and turn off the lights
};
