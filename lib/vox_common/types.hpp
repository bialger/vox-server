#ifndef VOX_COMMON_TYPES_HPP
#define VOX_COMMON_TYPES_HPP

#include <cstdint>
#include <expected>
#include <string>
#include <variant>

namespace vox::common {

using UserId = std::string;
using DeviceId = std::string;
using ConversationId = std::string;
using EnvelopeId = std::string;
using AttachmentId = std::string;
using SessionToken = std::string;
using Timestamp = std::int64_t;

enum class ConversationType : std::uint8_t {
  kDm = 0,
  kGroup = 1,
  kChannel = 2,
};

enum class MemberRole : std::uint8_t {
  kOwner = 0,
  kAdmin = 1,
  kMember = 2,
};

enum class ErrorCode : std::uint16_t {
  kOk = 0,
  kNotFound = 1,
  kAlreadyExists = 2,
  kUnauthorized = 3,
  kForbidden = 4,
  kInvalidArgument = 5,
  kInternal = 6,
  kQuotaExceeded = 7,
  kRateLimited = 8,
  kShutdown = 9,
  kQueueFull = 10,
  kExpired = 11,
  kDuplicate = 12,
};

struct Error {
  ErrorCode code;
  std::string message;
};

template<typename T>
using Result = std::expected<T, Error>;

using VoidResult = std::expected<void, Error>;

} // namespace vox::common

#endif // VOX_COMMON_TYPES_HPP
