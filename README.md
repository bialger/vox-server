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
| `vox_admin` | Administration service: optional HTTP admin (`X-Admin-Token`), server stats, cascading user deletion, force logout |
| `vox_net` | HTTP/1.1 (`Boost.Beast`) + JSON (`Boost.JSON`) API under `/v1/`, public `GET /v1/health`, WebSocket at `/v1/ws` with `Authorization: Bearer` on upgrade or a post-handshake `{"type":"auth",...}` frame for push notifications |

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
cmake -S . -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Release
```

### 2. Build the server binary

```shell
cmake --build build --target vox-server
```

### 3. Run the server

- Windows: `.\build\bin\vox-server.exe`
- Linux/macOS: `./build/bin/vox-server`

### Graceful shutdown

Press **Ctrl+C** in the terminal, or send **`SIGINT`** / **`SIGTERM`** (on Windows, **`SIGBREAK`** is also registered). The server closes the listening socket, stops the `io_context`, and worker threads exit their `run()` loop so the process can terminate cleanly.

### Command-line options

| Option | Description |
|--------|-------------|
| `--help`, `-h` | Show usage |
| `--config <path>` | Load `key=value` settings from a file (also `VOX_CONFIG_FILE`, or `vox.conf` in the current directory if it exists). Later sources override earlier ones for the same setting: file, then env (`VOX_SESSION_PEPPER`, `VOX_ADMIN_TOKEN`), then CLI. |
| `--listen <addr>` | Bind address (default: `127.0.0.1`) |
| `--port <n>` | TCP port (default: `8080`) |
| `--db <path>` | SQLite database file path |
| `--blobs <path>` | Directory for encrypted attachment blobs |
| `--threads <n>` | Number of `io_context` worker threads (default: `network_thread_count` in config) |
| `--session-pepper <secret>` | Secret used to HMAC session tokens in the database (required; can use env `VOX_SESSION_PEPPER` instead). Changing it invalidates existing sessions. |
| `--admin-token <secret>` | Enables `GET /v1/admin/stats` and `DELETE /v1/admin/users/{id}` with header `X-Admin-Token` |

### TLS and reverse proxy

The binary listens on plain TCP. For production, terminate **HTTPS** and **WebSocket over TLS** in a reverse proxy (e.g. Caddy or nginx) and forward to the local HTTP port.

**Is WSS automatic when the site uses HTTPS?** Not by magic: the **client** must open a WebSocket with a **`wss://`** URL (or a relative URL that resolves to `wss` on an HTTPS page). Serving the web app over HTTPS does not rewrite `ws://` to `wss://` by itself. Browsers treat `wss://host/v1/ws` like HTTPS to `host`—TLS to the proxy, then the proxy speaks plain HTTP/WebSocket to the app container. Configure TLS on **443** in the proxy; until then only `ws://` to the app (or unencrypted traffic) is possible from outside.

**WebSocket auth:** Use **`Authorization: Bearer`** on the upgrade request, or send **`{"type":"auth","access_token":"..."}`** as the first text frame within 5 seconds. The bundled Docker nginx uses **`log_format vox_noquery`** in **`deploy/nginx/nginx.conf`** so access logs omit query strings from the request line (useful if other routes add sensitive `?` parameters).

## Production deployment (Docker + nginx)

The repository includes a **multi-stage Dockerfile**, **`deploy/docker-compose.yml`** (application + **nginx** reverse proxy), and a **GitHub Actions** workflow that SSHs into your server, runs **`git pull`**, and rebuilds/restarts containers.

Default public hostname in the bundled nginx config is **`messenger.bialger.com`** (HTTP on port **80**). The app listens on **`0.0.0.0:8080`** inside the Docker network only; nginx is the public entrypoint.

### What runs in Compose

| Service     | Role |
|-------------|------|
| `vox-server` | Built from `deploy/Dockerfile`; data in volume `vox_data` (`/data`: SQLite DB + attachment blobs). **`deploy/vox.conf`** is bind-mounted to **`/etc/vox.conf`** (see **`deploy/vox.conf.example`**); secrets **`VOX_SESSION_PEPPER`** / **`VOX_ADMIN_TOKEN`** stay in **`deploy/.env`**. |
| `nginx`     | Reverse proxy to `vox-server:8080`, WebSocket upgrade headers, `client_max_body_size 100m`, ACME webroot for Let’s Encrypt; **`deploy/nginx/nginx.conf`** sets access logging **without** query strings (see **TLS and reverse proxy** above). |
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
| **`ci_tests.yml`** | **`push`** to any branch | Job **`build-linux`** (Ubuntu) builds, runs tests, uploads **`vox-server-linux`**. Reusable **`ci`** runs Windows + style + tidy in parallel. On **`master`**, **`release`** runs **only after** **`build-linux`** (image from artifact + SSH deploy), without waiting for Windows or static checks. |
| **`ci_deploy_pr_branch.yml`** | After **`CI tests`** succeeds **`push`**, or on **`pull_request`** into **`master`** | Job **`deploy-pr`**: image + SSH deploy in **one** job when there is an **open** (non-draft) PR to **`master`** from the same repo, or after CI when that PR already exists. Same-repo only; not for forks. |

**Image + deploy** — jobs **`release`** (**`ci_tests.yml`**, **`master`** only) and **`deploy-pr`** (**`ci_deploy_pr_branch.yml`**, feature branch + PR to **`master`**):

- Job **`build-linux`** uploads **`vox-server-linux`**. **`release`** / **`deploy-pr`** download it, build **`deploy/Dockerfile.prebuilt`**, push **`ghcr.io/<owner>/<repo>:<sha>`** and **`:latest`**, then SSH to **`messenger.bialger.com`**, **`git pull`** in **`/opt/vox-server`** (nginx / compose only — **no C++ compile on the server**).
- **`deploy/.env`**: only ensures **`VOX_IMAGE=ghcr.io/...:<sha>`** (creates the file if missing, or appends **`VOX_IMAGE`** if absent; if **`VOX_IMAGE`** was already present, removes the old line and appends the new value at the end). **Does not** change **`VOX_ADMIN_TOKEN`** or other variables.
- **`docker login ghcr.io`** when **`GHCR_READ_TOKEN`** is set,
- **`docker compose pull`** and **`docker compose up -d`**.

**Manual** — **`.github/workflows/deploy.yml`** (**Actions → Deploy (Docker) → Run workflow**):

- Inputs: **`server_host`**, **`deploy_path`**, **`branch`**, **`image_tag`** (use the commit SHA from the workflow that built the image, or **`latest`** if you only track **`master`**).

**CI / GHCR** uses **`deploy/Dockerfile.prebuilt`**: only the runtime base image and the **`vox-server`** binary produced in CI. **Local** builds use **`deploy/docker-compose.build.yml`** and **`deploy/Dockerfile`**, which compiles the app in a builder stage with **Boost**, **fmt**, **spdlog**, **SQLiteCpp**, **GoogleTest**, **libargon2** from apt and **`-DVOX_USE_SYSTEM_DEPS=ON`**, then copies the binary into the runtime layer.

#### Troubleshooting: no deploy on a PR, or “Run workflow” is missing

1. **PR from a fork** — **`push`** workflows do **not** run on the fork in your repo; fork PRs are **not** covered by this project’s **`CI tests`** automation. Use a branch **in the same repository** for automatic image + deploy on PRs, or merge from a fork and deploy after the code is on **`master`** (or use **manual** **`deploy.yml`**).

2. **Manual run not listed** — GitHub only shows **Actions → Run workflow** for workflows whose YAML **already exists on the default branch** (`master`). If **`deploy.yml`** / CI changes exist only on a feature branch, merge them into **`master`** first; then **`Deploy (Docker)`** appears in the sidebar.

3. **First-time Actions** — In **Settings → Actions → General**, ensure **Actions** are allowed for the repository (and for fork PRs, whether workflows need approval).

### Local / dev: build the image on your machine

Use **`deploy/docker-compose.build.yml`** so Compose still has a **`build:`** section:

```shell
cd /opt/vox-server
docker compose -f deploy/docker-compose.yml -f deploy/docker-compose.build.yml --project-directory deploy up --build
```

### Admin token: enable, change, and disable

Automated deploy **only** updates **`VOX_IMAGE`** in **`deploy/.env`**; it does **not** set **`VOX_ADMIN_TOKEN`** or **`VOX_SESSION_PEPPER`**.

**Session pepper (required for Docker):** set **`VOX_SESSION_PEPPER`** to a long random secret in **`deploy/.env`**. The server uses it to HMAC session tokens; changing it logs everyone out.

**Enable or change** — on the server, edit **`deploy/.env`** (or use Compose overrides):

```shell
cd /opt/vox-server/deploy
cp -n .env.example .env
# Add: VOX_SESSION_PEPPER=your-long-random-secret
# Optional: VOX_ADMIN_TOKEN=your-secret
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
4. Reload nginx (and recreate if needed so **`443:443`** is bound): **`docker compose up -d`** then **`docker compose exec nginx nginx -s reload`**
5. Optionally add a small HTTP server block that returns **`301`** to HTTPS only after you confirm TLS works.

**Note:** **`deploy/docker-compose.yml`** already maps **`80:80`** and **`443:443`** for nginx. Until **`20-ssl.conf`** exists, nothing listens on **443** inside the container; publishing the port is harmless and avoids editing compose only on the server.

Web clients should use **`https://`** and **`wss://`** for the same host and `/v1/...` paths.

### nginx: omit query strings from access logs (bundled setup)

The repo ships **`deploy/nginx/nginx.conf`**, mounted into the **`nginx`** service, with:

- **`log_format vox_noquery`** — same idea as the common **`combined`** format, but the request line is logged as **`"$request_method $uri $server_protocol"`** instead of **`"$request"`**, so query strings are **not** written to **`/var/log/nginx/access.log`**.
- A single **`access_log`** at **`http`** level using that format (avoids the stock image’s default **`main`** format that would log full **`$request`**).

If you add your own **`access_log`** directives in **`conf.d`**, avoid re-enabling a format that uses **`$request`**, or you will log tokens again. For **other** reverse proxies (Caddy, Traefik, Envoy, cloud load balancers), configure access logging to **drop or redact** the query part; many examples use a custom log line template analogous to nginx’s **`$uri`**-only style.

### Changing the public hostname

Edit **`server_name`** in **`deploy/nginx/conf.d/10-vox.conf`** (and in the SSL example), update DNS, and re-issue certificates if you use Let’s Encrypt.

## HTTP API (version `v1`)

Full reference (request/response shapes, auth, errors, WebSocket, health): **[API.md](API.md)**.

Summary: JSON bodies use `Content-Type: application/json`. Authenticated routes use `Authorization: Bearer <access_token>` unless noted. Admin routes use `X-Admin-Token` when the server is configured with an admin token. Unauthenticated **`GET /v1/health`** returns `{"status":"ok"}` for probes (no Bearer).

## SDUI (EULA + update) configuration

The server exposes SDUI endpoints (see `API.md`) and can show a public (unauth) screen with:

- EULA text (loaded from a file)
- repository link
- optional soft update prompt

### Files to edit on the server (Docker deployment)

1. Create an EULA file:

- Start from [`deploy/eula.example.txt`](deploy/eula.example.txt)
- Copy to `deploy/eula.txt` on the server (this repo does not ship `deploy/eula.txt` by default)

2. Configure SDUI keys in `deploy/vox.conf`:

Use [`deploy/vox.conf.example`](deploy/vox.conf.example) as a reference. Required / common keys:

- `sdui_eula_path = /etc/vox/eula.txt`
- `sdui_eula_version = 2026-04-23` (bump when the text changes)
- `sdui_repo_url = https://github.com/bialger/VoxMessenger`
- `sdui_android_store_url = ...` (optional; if empty, Update button falls back to `sdui_repo_url`)
- `sdui_latest_client_version_code = 0` (optional; set to a number to show a soft update prompt)

3. Ensure Compose mounts the EULA file:

`deploy/docker-compose.yml` mounts `./eula.txt` into the container at `/etc/vox/eula.txt`.

## How to build and run tests

### Unit tests (no Boost)

Useful for fast CI or when Boost is not built:

```shell
cmake -S . -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Release -DTESTS_ONLY=ON
cmake --build build --target vox-server_tests
```

Run:

- Windows: `.\build\tests\vox-server_tests.exe`
- Linux/macOS: `./build/tests/vox-server_tests`

### Full integration tests (including HTTP + Boost)

Requires a full configure **without** `-DTESTS_ONLY=ON` so `vox_net` and Boost are available:

```shell
cmake -S . -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Release
cmake --build build --target vox-server_tests
cmake --build build --target vox-server_net_tests
```

Run:

- `vox-server_net_tests` — HTTP integration tests against a live listener on an ephemeral port.

Or via CTest:

```shell
cd build && ctest --output-on-failure
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
│   ├── nginx/                    # nginx.conf (access log without query) + conf.d/
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
├── API.md                        # HTTP/WebSocket API reference (v1), incl. GET /v1/health
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
