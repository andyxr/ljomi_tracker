#pragma once
#include "FluxEngine.hpp"
#include <cstddef>
#include <cstdint>
#include <string>

namespace tapejam {
// Decode a WAV file held in memory to 16-bit stereo. Returns nullptr on failure.
Sample* decodeWav(const uint8_t* data, size_t size, std::string* err);
}
