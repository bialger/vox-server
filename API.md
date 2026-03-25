# Vox Server HTTP API (`v1`)

The server exposes JSON over HTTP/HTTPS. Terminate TLS (`https://`, `wss://`) in a reverse proxy (e.g. nginx); the application listens on plain TCP.

**Base path:** all routes documented below are under the `/v1` prefix unless otherwise noted.

**Content type:** request bodies with JSON use `Content-Type: application/json`. Responses are JSON unless the route returns a binary attachment.

---

## Authentication

### Bearer token (user API)

Most routes require a valid access token:

```http
Authorization: Bearer <access_token>
```

Obtained from `POST /v1/register`, `POST /v1/login`, or `POST /v1/refresh`. If the token is missing, invalid, or expired, the server responds with **401** and a JSON error body.

### Admin API

Routes under `/v1/admin/*` require the shared secret header (no Bearer session):

```http
X-Admin-Token: <server admin token>
```

Enabled only when the server is started with `--admin-token` (or `VOX_ADMIN_TOKEN` in Docker). The same `Authorization` rules do not apply.

---

## Error responses

JSON errors follow this shape:

```json
{
  "error": {
    "code": <integer>,
    "message": "<string>"
  }
}
```

`code` is a server `ErrorCode` (see `lib/vox_common/types.hpp`). Typical HTTP status mapping:

| HTTP | Meaning (typical) |
|------|-------------------|
| 400 | Invalid argument |
| 401 | Unauthorized / expired token |
| 403 | Forbidden |
| 404 | Not found |
| 409 | Conflict (already exists / duplicate) |
| 413 | Quota exceeded |
| 429 | Rate limited |
| 500 | Internal error |
| 503 | Queue full (offline cap) |

---

## Public endpoints (no Bearer)

### `GET /v1/health`

Liveness probe: no `Authorization` header and no request body.

**200 response:** `Content-Type: application/json`

```json
{ "status": "ok" }
```

Example:

```bash
curl -sS https://<host>/v1/health
```

Use this for load balancers, uptime checks, or a quick browser/curl sanity check. Auth routes such as `POST /v1/login` are **POST-only**; opening them in a tab issues **GET** and will not hit the login handler (you may see **404** for unknown paths without Bearer, not **401**).

---

### `POST /v1/register`

Create a user and device session.

**Body (JSON object):**

| Field | Type | Description |
|--------|------|-------------|
| `username` | string | |
| `password_derived_value` | string | Client-derived secret (e.g. Argon2 input), not necessarily the raw password |
| `device_id` | string | |
| `identity_key_public` | string | |
| `signed_prekey_public` | string | |
| `signed_prekey_signature` | string | |

**200 response:** `{ "user_id", "access_token", "refresh_token" }`

---

### `POST /v1/login`

**Body:** `{ "username", "password_derived_value", "device_id" }`

**200 response:** `{ "user_id", "access_token", "refresh_token" }`

---

### `POST /v1/refresh`

**Body:** `{ "refresh_token", "device_id" }`

**200 response:** `{ "access_token", "refresh_token" }`

---

## Authenticated endpoints (Bearer required)

Unless stated, `Authorization: Bearer <access_token>` is required. Missing or invalid Bearer yields **401** before route logic.

### `POST /v1/logout`

Revokes the current session.

**200 response:** `{}`

---

### `POST /v1/messages/send`

Send an encrypted envelope to a conversation.

**Body:**

| Field | Type | Description |
|--------|------|-------------|
| `device_id` | string | **Must equal** the session’s device ID |
| `conversation_id` | string | |
| `ciphertext` | string | |
| `envelope_id` | string | Client-generated id |
| `envelope_type` | integer | Optional |

**200 response:** `{ "envelope_id", "server_timestamp", "delivered_to_count" }`

---

### `POST /v1/messages/ack`

Acknowledge delivery of an envelope.

**Body:** `{ "device_id" }` (must match session device), `{ "envelope_id" }`

**200 response:** `{}`

---

### `GET /v1/sync/pending`

Offline queue: list envelopes pending for the connected device.

**Query:** `limit` (optional, default 100)

**200 response:**

```json
{
  "envelopes": [
    {
      "envelope_id": "",
      "conversation_id": "",
      "sender_device_id": "",
      "ciphertext": "",
      "server_timestamp": 0,
      "envelope_type": 0
    }
  ]
}
```

---

### `GET /v1/conversations/{conversation_id}/envelopes`

History for a conversation the user belongs to.

**Query:** `since` (optional timestamp), `limit` (optional, default 100)

**200 response:** `{ "envelopes": [ ... ] }` (same envelope object shape as sync).

**403** if the user is not a member.

---

### `GET /v1/conversations`

List conversations for the current user.

**200 response:**

```json
{
  "conversations": [
    {
      "conversation_id": "",
      "type": 0,
      "created_by": "",
      "created_at": 0
    }
  ]
}
```

**`type` (integer):** `0` = DM, `1` = group, `2` = channel (see `ConversationType` in `lib/vox_common/types.hpp`).

---

### `POST /v1/conversations`

Create a conversation.

**Body — `type`: `"dm"`**

| Field | Type |
|--------|------|
| `type` | `"dm"` |
| `peer_user_id` | string |

**200:** `{ "conversation_id" }`

**Body — `type`: `"group"`**

| Field | Type |
|--------|------|
| `type` | `"group"` |
| `members` | array of user id strings |

**200:** `{ "conversation_id" }`

**Body — `type`: `"channel"`**

| Field | Type |
|--------|------|
| `type` | `"channel"` |
| `admins` | array of user id strings (optional) |
| `subscribers` | array of user id strings (optional) |

**200:** `{ "conversation_id" }`

**400** if `type` is not `dm`, `group`, or `channel`.

---

### `POST /v1/conversations/{conversation_id}/members`

Add a member.

**Body:** `{ "user_id", "role" }` — `role` is `"owner"`, `"admin"`, or `"member"` (default `"member"`).

**200:** `{}`

---

### `DELETE /v1/conversations/{conversation_id}/members/{user_id}`

Remove a member.

**200:** `{}`

---

### `POST /v1/conversations/{conversation_id}/subscribe`

Subscribe the current user to a channel.

**200:** `{}`

---

### `POST /v1/conversations/{conversation_id}/unsubscribe`

Unsubscribe from a channel.

**200:** `{}`

---

### `POST /v1/devices/{device_id}/prekeys`

Upload one-time prekeys. **`device_id` must match the session device.**

**Body:**

```json
{
  "prekeys": [
    { "prekey_id": "", "prekey_public": "" }
  ]
}
```

**200:** `{}`

---

### `GET /v1/devices/{device_id}/prekey-bundle`

Fetch key material for establishing a session with the given device.

**200 response:** includes `identity_key_public`, `signed_prekey_public`, `signed_prekey_signature`, and optionally `one_time_prekey_public`, `one_time_prekey_id`.

---

## Attachments (Bearer required)

### `POST /v1/attachments/upload-init`

**Body:** `{ "conversation_id", "file_size", "mime_hint" }`

**200:** `{ "attachment_id", "blob_path" }`

---

### `PUT /v1/attachments/{attachment_id}/chunk?offset=<bytes>`

Upload a binary chunk. **Body:** raw bytes (not JSON). `offset` is the byte offset in the blob.

**200:** `{}`

---

### `POST /v1/attachments/{attachment_id}/finalize`

**Body:** `{ "ciphertext_hash" }`

**200:** `{}`

---

### `GET /v1/attachments/{attachment_id}`

Download the full attachment. **Response:** `application/octet-stream` (binary body).

---

## Admin (no Bearer; `X-Admin-Token` only)

### `GET /v1/admin/stats`

**200 response (example fields):** `user_count`, `device_count`, `active_session_count`, `conversation_count`, `pending_envelope_count`, `total_storage_bytes`

---

### `DELETE /v1/admin/users/{user_id}`

Delete a user by id.

**200:** `{}`

---

## WebSocket

### `GET /v1/ws?access_token=<access_token>`

Upgrade to WebSocket (same host as HTTP). Pass the **access token** as query parameter `access_token` (not `Authorization`).

After the upgrade, the server may push JSON messages such as:

```json
{
  "type": "envelope",
  "envelope_id": "",
  "conversation_id": "",
  "sender_device_id": "",
  "ciphertext": "",
  "server_timestamp": 0
}
```

when the delivery manager queues an envelope for the connected device.

---

## Not found

Any other method/path returns **404** (JSON body may be empty for simple 404 responses from the HTTP stack).
