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

## Production deployment (Docker + nginx)

The repository includes a **multi-stage Dockerfile**, **`deploy/docker-compose.yml`** (application + **nginx** reverse proxy), and a **GitHub Actions** workflow that SSHs into your server, runs **`git pull`**, and rebuilds/restarts containers.

Default public hostname in the bundled nginx config is **`messenger.bialger.com`** (HTTP on port **80**). The app listens on **`0.0.0.0:8080`** inside the Docker network only; nginx is the public entrypoint.

### What runs in Compose

| Service     | Role |
|-------------|------|
| `vox-server` | Built from `deploy/Dockerfile`; data in volume `vox_data` (`/data`: SQLite DB + attachment blobs). |
| `nginx`     | Reverse proxy to `vox-server:8080`, WebSocket upgrade headers, `client_max_body_size 100m`, ACME webroot for Let’s Encrypt. |
| `certbot`   | Optional profile `certbot` — used for one-off certificate issuance (see below). |

### Server preparation (before the first workflow run)

Do this once on the target machine (typical: Ubuntu 22.04/24.04 LTS).

1. **Install Docker Engine** and the **Docker Compose plugin** ([official docs](https://docs.docker.com/engine/install/)).
2. **Firewall**: allow inbound **22** (SSH), **80** (HTTP / ACME), and **443** when you enable HTTPS.
3. **DNS**: create an **A** (or **AAAA**) record so **`messenger.bialger.com`** points to this server’s public IP (change the name in `deploy/nginx/conf.d/10-vox.conf` if you use another host).
4. **Clone this repository** to a fixed path (the workflow default is **`/opt/vox-server`**):

   ```shell
   sudo mkdir -p /opt/vox-server
   sudo chown "$USER":"$USER" /opt/vox-server
   git clone https://github.com/<org>/vox-server.git /opt/vox-server
   ```

   For a **private** repository, configure a [deploy key](https://docs.github.com/en/authentication/connecting-to-github-with-ssh/managing-deploy-keys/deploy-keys) or HTTPS access so `git pull` on the server can reach GitHub without prompts.

5. **GitHub Container Registry (GHCR)** — the application image is **built on GitHub Actions** (not on the VPS) and pushed to **`ghcr.io/<owner>/<repo>`** with tags **`:<commit-sha>`** and **`:latest`** ( **`latest`** is updated on every successful build, including PRs).  
   - **Private packages**: the server must authenticate to `ghcr.io` when pulling. Add a **classic PAT** or fine-grained token with **`read:packages`** on the account that owns the package; store it as **`GHCR_READ_TOKEN`**. Optionally set **`GHCR_USERNAME`** to that GitHub username (defaults to the repository owner).  
   - **Public packages**: you can omit **`GHCR_READ_TOKEN`**; `docker compose pull` works without login.

6. **SSH access for GitHub Actions**: the deploy workflow uses **password** authentication. Create a dedicated Linux user with Docker permissions (e.g. membership in group `docker`) and a strong password; store that password only in **`SERVER_PASSWORD`** (see below).

### GitHub repository secrets

Define these in the repo: **Settings → Secrets and variables → Actions**.

| Secret | Required | Purpose |
|--------|----------|---------|
| **`SERVER_LOGIN`** | Yes | SSH username on the server (e.g. `deploy` or your admin user). |
| **`SERVER_PASSWORD`** | Yes | SSH password for that user (used only by the deploy workflow). |
| **`GHCR_READ_TOKEN`** | If the GHCR package is **private** | **GitHub PAT** with `read:packages` so the server can **`docker pull`** from `ghcr.io`. Not needed for **public** images. |
| **`GHCR_USERNAME`** | No | GitHub username for `docker login`; defaults to the repository owner. |

Admin API token (**`VOX_ADMIN_TOKEN`**) is **not** a GitHub secret for deploy: set it only in **`deploy/.env`** on the server if needed.

### GitHub Actions: when deploy runs

**CI** is defined in **`.github/workflows/ci-reusable.yml`** and runs **once per triggering event** (no duplicate **push** + **pull_request** for the same change):

| Workflow | Trigger | Role |
|---|---|---|
| **`ci_tests.yml`** | **`push`** to any branch | Runs reusable CI, then **`docker-publish`** + **`deploy-server`** only when the branch is **`master`**. |
| **`ci_fork_pr.yml`** | **`pull_request`** into **`master`** | Runs the same reusable CI **only** when the PR head is a **fork** (forks do not get **`push`** events on your repo). |
| **`ci_deploy_pr_branch.yml`** | After **`CI tests`** completes successfully | If the push was to a **non-`master`** branch and there is an **open PR** from that branch into **`master`**, builds the image and deploys (same idea as before, without a second full test run). |

**Image build** — job **`docker-publish`** in **`ci_tests.yml`** (after CI; **`push`** to **`master`** only) or in **`ci_deploy_pr_branch.yml`** (PR branch with open PR to **`master`**):

- Builds **`deploy/Dockerfile`** on a **GitHub-hosted runner** and pushes to **`ghcr.io/<lowercase owner>/<lowercase repo>:<sha>`** and updates **`:latest`** to that image.

**Deploy** — job **`deploy-server`** (same conditions as the image build above):

- SSHs to **`messenger.bialger.com`**, **`git pull`** in **`/opt/vox-server`** (nginx / compose files only — **no C++ compile on the server**),
- **`deploy/.env`**: only ensures **`VOX_IMAGE=ghcr.io/...:<sha>`** (creates the file if missing, or appends **`VOX_IMAGE`** if absent; if **`VOX_IMAGE`** was already present, removes the old line and appends the new value at the end). **Does not** change **`VOX_ADMIN_TOKEN`** or other variables.
- **`docker login ghcr.io`** when **`GHCR_READ_TOKEN`** is set,
- **`docker compose pull`** and **`docker compose up -d`**.

**Manual** — **`.github/workflows/deploy.yml`** (**Actions → Deploy (Docker) → Run workflow**):

- Inputs: **`server_host`**, **`deploy_path`**, **`branch`**, **`image_tag`** (use the commit SHA from the workflow that built the image, or **`latest`** if you only track **`master`**).

The Docker **builder** in **`deploy/Dockerfile`** installs **Boost**, **fmt**, **spdlog**, **SQLiteCpp**, **GoogleTest**, and **libargon2** from apt with **`-DVOX_USE_SYSTEM_DEPS=ON`**. The **runtime** image installs matching shared libraries (e.g. Boost, `libfmt9`, `libspdlog1.12`, `libargon2-1`, SQLite).

#### Troubleshooting: no deploy on a PR, or “Run workflow” is missing

1. **PR from a fork** — CI runs via **`ci_fork_pr.yml`**; **`docker-publish`** / **`deploy-server`** do **not** run for forks (secrets / trust). Use a branch **in the same repository** for automatic image + deploy on PRs, or merge from a fork and deploy after the code is on **`master`** (or use **manual** **`deploy.yml`**).

2. **Manual run not listed** — GitHub only shows **Actions → Run workflow** for workflows whose YAML **already exists on the default branch** (`master`). If **`deploy.yml`** / CI changes exist only on a feature branch, merge them into **`master`** first; then **`Deploy (Docker)`** appears in the sidebar.

3. **First-time Actions** — In **Settings → Actions → General**, ensure **Actions** are allowed for the repository (and for fork PRs, whether workflows need approval).

### Local / dev: build the image on your machine

Use **`deploy/docker-compose.build.yml`** so Compose still has a **`build:`** section:

```shell
cd /opt/vox-server
docker compose -f deploy/docker-compose.yml -f deploy/docker-compose.build.yml --project-directory deploy up --build
```

### Admin token: enable, change, and disable

Automated deploy **only** updates **`VOX_IMAGE`** in **`deploy/.env`**; it does **not** set **`VOX_ADMIN_TOKEN`**.

**Enable or change** — on the server, edit **`deploy/.env`** (or use Compose overrides):

```shell
cd /opt/vox-server/deploy
cp -n .env.example .env
# Add or edit: VOX_ADMIN_TOKEN=your-secret
docker compose up -d --force-recreate vox-server
```

**Disable admin** — remove **`VOX_ADMIN_TOKEN`** from **`deploy/.env`** (or leave it empty), then recreate the container:

```shell
cd /opt/vox-server/deploy
docker compose up -d --force-recreate vox-server
```

### HTTPS (Let’s Encrypt)

1. Ensure DNS and port **80** work (nginx serves `/.well-known/acme-challenge/` from the `certbot-www` volume).
2. On the server:

   ```shell
   cd /opt/vox-server/deploy
   chmod +x scripts/init-letsencrypt.sh
   ./scripts/init-letsencrypt.sh your@email.example
   ```

   Or run certbot manually:

   ```shell
   docker compose --profile certbot run --rm certbot certonly \
     --webroot -w /var/www/certbot \
     -d messenger.bialger.com \
     --email your@email.example \
     --agree-tos \
     --non-interactive
   ```

3. Copy **`deploy/nginx/conf.d/20-ssl.conf.example`** to **`deploy/nginx/conf.d/20-ssl.conf`**, adjust `server_name` and paths if needed.
4. Add **`443:443`** under the `nginx` service **`ports:`** in **`deploy/docker-compose.yml`** if it is not already published.
5. Reload nginx: **`docker compose exec nginx nginx -s reload`**
6. Optionally add a small HTTP server block that returns **`301`** to HTTPS only after you confirm TLS works.

Web clients should use **`https://`** and **`wss://`** for the same host and `/v1/...` paths.

### Changing the public hostname

Edit **`server_name`** in **`deploy/nginx/conf.d/10-vox.conf`** (and in the SSL example), update DNS, and re-issue certificates if you use Let’s Encrypt.

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
├── deploy/                       # Docker + nginx + deploy scripts
│   ├── Dockerfile
│   ├── docker-compose.yml
│   ├── docker-compose.build.yml  # optional local `docker compose build`
│   ├── docker-entrypoint.sh
│   ├── nginx/
│   └── scripts/
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
