# vox-server

Server side of the **Vox** secure messaging platform, built with C++23 and real multithreading.

Vox is a privacy-first, end-to-end encrypted messenger designed for self-hosted deployment. The server is content-blind: it relays, stores, and authorizes access to encrypted data but never sees plaintext messages and never stores users' private E2EE keys.

## Modules

| Module | Description |
|---|---|
| `vox_common` | Threading infrastructure (ThreadPool, BoundedQueue, ShardMap), configuration, types, UUID generation, logging |
| `vox_store` | SQLite persistence layer with repository classes for users, devices, sessions, conversations, envelopes, and attachments |
| `vox_auth` | Authentication service: Argon2id password hashing, opaque token management, registration/login/logout/refresh |
| `vox_relay` | Message relay (`SendEnvelope`), `ConversationService` for DM/group/channel creation, sharded delivery queues, offline fallback, membership-checked fanout |
| `vox_attachments` | Encrypted attachment management: chunked upload, quota enforcement, authorization, expiry cleanup |
| `vox_admin` | Administration service: server stats, cascading user deletion, force logout |
| `vox_net` | HTTP/1.1 (`Boost.Beast`) + JSON (`Boost.JSON`) API under `/v1/`, WebSocket at `/v1/ws?access_token=...` for push notifications |

## Dependencies

Libraries pulled via CMake `FetchContent` (see `lib/*/CMakeLists.txt`):

- [fmt](https://github.com/fmtlib/fmt) 12.1.0
- [spdlog](https://github.com/gabime/spdlog) v1.17.0
- [SQLiteCpp](https://github.com/SRombauts/SQLiteCpp) 3.3.3 (includes SQLite3 amalgamation)
- [Argon2](https://github.com/P-H-C/phc-winner-argon2) 20190702
- [Google Test](https://github.com/google/googletest) v1.14.0

**Boost** (1.83+): required for the `vox-server` binary and `vox-server_net_tests`. The project uses `lib/boost/CMakeLists.txt` to download and build Boost with `b2` when not found in the environment, or links against a system Boost.

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

### Graceful shutdown

Press **Ctrl+C** in the terminal, or send **`SIGINT`** / **`SIGTERM`** (on Windows, **`SIGBREAK`** is also registered). The server closes the listening socket, stops the `io_context`, and worker threads exit their `run()` loop so the process can terminate cleanly.

### Command-line options

| Option | Description |
|--------|-------------|
| `--help`, `-h` | Show usage |
| `--listen <addr>` | Bind address (default: `127.0.0.1`) |
| `--port <n>` | TCP port (default: `8080`) |
| `--db <path>` | SQLite database file path |
| `--blobs <path>` | Directory for encrypted attachment blobs |
| `--threads <n>` | Number of `io_context` worker threads (default: `network_thread_count` in config) |
| `--admin-token <secret>` | Enables `GET /v1/admin/stats` and `DELETE /v1/admin/users/{id}` with header `X-Admin-Token` |

### TLS and reverse proxy

The binary listens on plain TCP. For production, terminate HTTPS and WSS in a reverse proxy (e.g. Caddy or nginx) and forward to the local HTTP port.

## HTTP API (version `v1`)

All JSON bodies use `Content-Type: application/json`. Authenticated routes expect `Authorization: Bearer <access_token>` unless noted.

| Area | Method | Path | Notes |
|------|--------|------|--------|
| Auth | POST | `/v1/register` | Public |
| Auth | POST | `/v1/login` | Public |
| Auth | POST | `/v1/refresh` | Public (refresh token + device id in body) |
| Auth | POST | `/v1/logout` | Bearer required; revokes current session |
| Messages | POST | `/v1/messages/send` | `device_id` must match session |
| Messages | POST | `/v1/messages/ack` | |
| Sync | GET | `/v1/sync/pending?limit=` | Offline queue |
| History | GET | `/v1/conversations/{id}/envelopes?since=&limit=` | |
| Conversations | GET | `/v1/conversations` | |
| Conversations | POST | `/v1/conversations` | Body: `type` = `dm` \| `group` \| `channel` (see code) |
| Members | POST | `/v1/conversations/{id}/members` | |
| Members | DELETE | `/v1/conversations/{id}/members/{user_id}` | |
| Channel | POST | `/v1/conversations/{id}/subscribe` | |
| Channel | POST | `/v1/conversations/{id}/unsubscribe` | |
| Prekeys | POST | `/v1/devices/{device_id}/prekeys` | Own device only |
| Prekeys | GET | `/v1/devices/{device_id}/prekey-bundle` | |
| Attachments | POST | `/v1/attachments/upload-init` | |
| Attachments | PUT | `/v1/attachments/{id}/chunk?offset=` | Raw body bytes |
| Attachments | POST | `/v1/attachments/{id}/finalize` | Body: `ciphertext_hash` |
| Attachments | GET | `/v1/attachments/{id}` | Binary |
| Admin | GET | `/v1/admin/stats` | `X-Admin-Token` only (no Bearer) |
| Admin | DELETE | `/v1/admin/users/{id}` | `X-Admin-Token` only |

**WebSocket:** `GET /v1/ws?access_token=<access_token>` — after upgrade, the server sends JSON lines with `type: envelope` when a message is queued for the device (see `DeliveryManager` enqueue hook).

## How to build and run tests

### Unit tests (no Boost)

Useful for fast CI or when Boost is not built:

```shell
cmake -S . -B cmake-build -G "Ninja" -DCMAKE_BUILD_TYPE=Release -DTESTS_ONLY=ON
cmake --build cmake-build --target vox-server_tests
```

Run:

- Windows: `.\cmake-build\tests\vox-server_tests.exe`
- Linux/macOS: `./cmake-build/tests/vox-server_tests`

### Full integration tests (including HTTP + Boost)

Requires a full configure **without** `-DTESTS_ONLY=ON` so `vox_net` and Boost are available:

```shell
cmake -S . -B cmake-build -G "Ninja" -DCMAKE_BUILD_TYPE=Release
cmake --build cmake-build --target vox-server_tests
cmake --build cmake-build --target vox-server_net_tests
```

Run:

- `vox-server_net_tests` — HTTP integration tests against a live listener on an ephemeral port.

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
│   ├── vox_common/
│   ├── vox_store/
│   ├── vox_auth/
│   ├── vox_relay/                # relay_service, conversation_service, delivery_manager
│   ├── vox_attachments/
│   ├── vox_admin/
│   └── vox_net/                  # HTTP listener, dispatch, WebSocket, ws registry
├── tests/
│   ├── test_suites/              # Fixtures (e.g. NetApiTestSuite)
│   ├── *_tests.cpp
│   └── net_api_tests.cpp         # HTTP integration tests
├── .github/workflows/
├── CMakeLists.txt
└── README.md
```

Include paths use the project root: `#include "lib/vox_store/database.hpp"` etc.

## Configuration reference

The `ServerConfig` struct in `lib/vox_common/config.hpp` controls runtime parameters:

| Parameter | Default | Description |
|---|---|---|
| `listen_address` | `127.0.0.1` | Bind address |
| `listen_port` | `8080` | TCP port |
| `network_thread_count` | `4` | `io_context` worker threads |
| `admin_token` | empty | If set, enables admin HTTP routes with `X-Admin-Token` |
| `cpu_pool_size` | 2 | Worker threads for CPU-heavy operations (Argon2 hashing) |
| `storage_pool_size` | 2 | Worker threads for database and blob I/O |
| `task_queue_capacity` | 1024 | Max pending tasks per thread pool |
| `max_group_size` | 256 | Maximum members in a group conversation |
| `max_channel_size` | 10000 | Subscriber cap for channels |
| `max_queue_depth_per_device` | 1000 | Max queued envelopes per device before offline fallback |
| `max_upload_size_bytes` | 100 MB | Maximum single attachment size |
| `max_storage_per_user_bytes` | 1 GB | Per-user storage quota |
| `access_token_lifetime_seconds` | 15 min | Access token TTL |
| `refresh_token_lifetime_seconds` | 30 days | Refresh token TTL |

## License

GNU General Public License v3.0
