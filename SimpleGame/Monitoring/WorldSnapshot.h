#pragma once
#include <cstdint>
#include <string>

namespace monitoring
{
// Called only on the game thread. Never holds references to mutable game state.
std::string BuildWorldSnapshot(std::uint64_t sequence, const std::string &sessionId);
} // namespace monitoring
