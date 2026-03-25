#ifndef VOX_COMMON_HMAC_SHA256_HPP
#define VOX_COMMON_HMAC_SHA256_HPP

#include <string>
#include <string_view>

namespace vox::common {

/// RFC 2104 HMAC-SHA256; returns 64 lowercase hex characters.
std::string HmacSha256Hex(std::string_view key, std::string_view message);

} // namespace vox::common

#endif // VOX_COMMON_HMAC_SHA256_HPP
