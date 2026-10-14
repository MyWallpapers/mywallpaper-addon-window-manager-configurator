#pragma once
#include "model.hpp"
#include <istream>
#include <optional>
#include <ostream>
namespace wm::wire {
inline constexpr uint32_t version = 5;
inline constexpr uint32_t max_chunk = 1024 * 1024;
std::optional<Json> Read(std::istream &input);
void Write(std::ostream &output, const Json &frame);
} // namespace wm::wire
