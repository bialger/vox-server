#ifndef VOX_COMMON_RANDOM_BYTES_HPP
#define VOX_COMMON_RANDOM_BYTES_HPP

#include <cstddef>
#include <cstdint>
#include <span>

namespace vox::common {

/// Fills `out` with cryptographically strong random bytes. Returns false on failure.
bool FillRandomBytes(std::span<std::uint8_t> out);

} // namespace vox::common

#endif // VOX_COMMON_RANDOM_BYTES_HPP
