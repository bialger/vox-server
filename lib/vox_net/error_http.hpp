#ifndef VOX_NET_ERROR_HTTP_HPP
#define VOX_NET_ERROR_HTTP_HPP

#include "lib/vox_common/types.hpp"

namespace vox::net {

inline unsigned HttpStatusForError(common::ErrorCode c) {
  switch (c) {
    case common::ErrorCode::kNotFound:
      return 404;
    case common::ErrorCode::kAlreadyExists:
      return 409;
    case common::ErrorCode::kUnauthorized:
      return 401;
    case common::ErrorCode::kForbidden:
      return 403;
    case common::ErrorCode::kInvalidArgument:
      return 400;
    case common::ErrorCode::kQuotaExceeded:
      return 413;
    case common::ErrorCode::kRateLimited:
      return 429;
    case common::ErrorCode::kExpired:
      return 401;
    case common::ErrorCode::kDuplicate:
      return 409;
    case common::ErrorCode::kQueueFull:
      return 503;
    default:
      return 500;
  }
}

} // namespace vox::net

#endif // VOX_NET_ERROR_HTTP_HPP
