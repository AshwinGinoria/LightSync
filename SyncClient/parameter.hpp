// Parameter.h
#pragma once
#include <variant>
#include <array>
#include <string>

using Parameter = std::variant<int, float, std::array<uint8_t, 3>, std::string>;
