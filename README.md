# vox-server

Server side of the **Vox** secure messaging platform, built with C++23 and real multithreading.

Vox is a privacy-first, end-to-end encrypted messenger designed for self-hosted deployment. The server is content-blind: it relays, stores, and authorizes access to encrypted data but never sees plaintext messages and never stores users' private E2EE keys.

## Modules

| Module | Description |
|---|---|
| `vox_common` | Threading infrastructure (ThreadPool, BoundedQueue, ShardMap), configuration, types, UUID generation, logging |
| `vox_store` | SQLite persistence layer with repository classes for users, devices, sessions, conversations, envelopes, and attachments |
| `vox_auth` | Authentication service: Argon2id password hashing, opaque token management, registration/login/logout/refresh |
| `vox_relay` | Message relay with sharded in-memory delivery queues, offline fallback, membership-checked fanout, duplicate detection |
| `vox_attachments` | Encrypted attachment management: chunked upload, quota enforcement, authorization, expiry cleanup |
| `vox_admin` | Administration service: server stats, cascading user deletion, force logout |

## Dependencies

All dependencies are fetched automatically via CMake `FetchContent`:

- [Boost](https://www.boost.org/) 1.83.0 (for the main server binary)
- [fmt](https://github.com/fmtlib/fmt) 12.1.0
- [spdlog](https://github.com/gabime/spdlog) v1.17.0
- [SQLiteCpp](https://github.com/SRombauts/SQLiteCpp) 3.3.3 (includes SQLite3 amalgamation)
- [Argon2](https://github.com/P-H-C/phc-winner-argon2) 20190702
- [Google Test](https://github.com/google/googletest) v1.14.0

## Prerequisites

- CMake 3.25+
- Ninja
- Git
- A C++23-capable compiler (GCC 13+, Clang 17+, or MSVC 2022+)

## How to build and run

Run the following commands from the project directory.

### 1. Create CMake cache

```shell
cmake -S . -B cmake-build -G "Ninja" -DCMAKE_BUILD_TYPE=Release
```

### 2. Build the server binary

```shell
cmake --build cmake-build --target vox-server
```

### 3. Run the server

- Windows: `.\cmake-build\bin\vox-server.exe`
- Linux/macOS: `./cmake-build/bin/vox-server`

## How to build and run tests

Tests can be built independently from the server binary (without Boost):

```shell
cmake -S . -B cmake-build -G "Ninja" -DCMAKE_BUILD_TYPE=Release -DTESTS_ONLY=ON
cmake --build cmake-build --target vox-server_tests
```

Run tests:

- Windows: `.\cmake-build\tests\vox-server_tests.exe`
- Linux/macOS: `./cmake-build/tests/vox-server_tests`

Or via CTest:

```shell
cd cmake-build && ctest --output-on-failure
```

## Project structure

```
vox-server/
├── bin/                          # Server executable entry point
│   └── main.cpp
├── lib/
│   ├── boost/                    # Boost download and build scripts
│   ├── vox_common/               # Threading, config, types, UUID, logging
│   │   ├── config.cpp, config.hpp
│   │   ├── uuid.cpp, uuid.hpp
│   │   ├── thread_pool.cpp, thread_pool.hpp
│   │   ├── logging.cpp, logging.hpp
│   │   └── bounded_queue.hpp, shard_map.hpp, types.hpp
│   ├── vox_store/                # SQLite database and repositories
│   │   ├── database.cpp, database.hpp
│   │   ├── user_repository.cpp, user_repository.hpp
│   │   ├── device_repository.cpp, device_repository.hpp
│   │   ├── session_repository.cpp, session_repository.hpp
│   │   ├── conversation_repository.cpp, conversation_repository.hpp
│   │   ├── envelope_repository.cpp, envelope_repository.hpp
│   │   └── attachment_repository.cpp, attachment_repository.hpp
│   ├── vox_auth/                 # Authentication (Argon2, tokens, auth service)
│   │   ├── password_hasher.cpp, password_hasher.hpp
│   │   ├── token_manager.cpp, token_manager.hpp
│   │   └── auth_service.cpp, auth_service.hpp
│   ├── vox_relay/                # Message relay and delivery queues
│   │   ├── delivery_manager.cpp, delivery_manager.hpp
│   │   └── relay_service.cpp, relay_service.hpp
│   ├── vox_attachments/          # Attachment management
│   │   └── attachment_service.cpp, attachment_service.hpp
│   └── vox_admin/                # Admin operations
│       └── admin_service.cpp, admin_service.hpp
├── tests/                        # Google Test suites
│   ├── test_suites/              # Test fixture classes
│   ├── thread_pool_tests.cpp
│   ├── store_tests.cpp
│   ├── auth_tests.cpp
│   ├── relay_tests.cpp
│   ├── attachment_tests.cpp
│   └── admin_tests.cpp
├── .github/workflows/            # CI configuration
├── CMakeLists.txt                # Root CMake configuration
└── README.md
```

Include paths use the project root: `#include "lib/vox_store/database.hpp"` etc.

## Configuration reference

The `ServerConfig` struct in `lib/vox_common/config.hpp` controls runtime parameters:

| Parameter | Default | Description |
|---|---|---|
| `cpu_pool_size` | 2 | Worker threads for CPU-heavy operations (Argon2 hashing) |
| `storage_pool_size` | 2 | Worker threads for database and blob I/O |
| `task_queue_capacity` | 1024 | Max pending tasks per thread pool |
| `max_group_size` | 256 | Maximum members in a group conversation |
| `max_queue_depth_per_device` | 1000 | Max queued envelopes per device before offline fallback |
| `max_upload_size_bytes` | 100 MB | Maximum single attachment size |
| `max_storage_per_user_bytes` | 1 GB | Per-user storage quota |
| `access_token_lifetime_seconds` | 15 min | Access token TTL |
| `refresh_token_lifetime_seconds` | 30 days | Refresh token TTL |

## License

GNU General Public License v3.0
