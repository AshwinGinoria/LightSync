#pragma once
#include <array>
#include <iostream>
#include <sstream>
#include <string>
#include <variant>

struct Parameter {
    using Value = std::variant<int, float, std::array<uint8_t, 3>, std::string>;
    Value value;

    Parameter() = default;
    Parameter(int v) : value(v) {}
    Parameter(float v) : value(v) {}
    Parameter(const std::array<uint8_t, 3> &v) : value(v) {}
    Parameter(const std::string &v) : value(v) {}
    Parameter(const char *v) : value(std::string(v)) {}

    // helper function to easily log parameters
    std::string to_string() const {
        return std::visit(
            [](const auto &val) -> std::string {
                using T = std::decay_t<decltype(val)>;
                if constexpr (std::is_same_v<T, int> ||
                              std::is_same_v<T, float>) {
                    return std::to_string(val);
                } else if constexpr (std::is_same_v<T, std::string>) {
                    return val;
                } else if constexpr (std::is_same_v<T,
                                                    std::array<uint8_t, 3>>) {
                    std::ostringstream oss;
                    oss << "(" << static_cast<int>(val[0]) << ", "
                        << static_cast<int>(val[1]) << ", "
                        << static_cast<int>(val[2]) << ")";
                    return oss.str();
                } else {
                    return "<unknown>";
                }
            },
            value);
    }

    float get_float() {
        if (!std::holds_alternative<float>(value)) {
            throw std::runtime_error("Parameter value is not a float");
        }

        return std::get<float>(value);
    }

    int get_int() {
        if (!std::holds_alternative<int>(value)) {
            throw std::runtime_error("Parameter value is not an integer");
        }
        return std::get<int>(value);
    }

    std::array<uint8_t, 3> get_color() {
        if (!std::holds_alternative<std::array<uint8_t, 3>>(value)) {
            throw std::runtime_error("Parameter value is not an color");
        }

        return std::get<std::array<uint8_t, 3>>(value);
    }

    // operator<< support
    friend std::ostream &operator<<(std::ostream &os, const Parameter &p) {
        return os << p.to_string();
    }
};
