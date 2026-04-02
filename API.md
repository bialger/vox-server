# Vox Server HTTP API (`v1`)

The server exposes JSON over HTTP/HTTPS. Terminate TLS (`https://`, `wss://`) in a reverse proxy (e.g. nginx); the application listens on plain TCP.

**Base path:** all routes documented below are under the `/v1` prefix unless otherwise noted.

**Content type:** request bodies with JSON use `Content-Type: application/json`. Responses are JSON unless the route returns a binary attachment.

**Schemas:** below each operation, formal **path** / **query** / **request body** / **response** tables summarize fields (OpenAPI-style). Existing JSON examples are kept as illustrations.

---

## Authentication

### Bearer token (user API)

Most HTTP routes require:

| Header          | Value                     |
| --------------- | ------------------------- |
| `Authorization` | `Bearer <access_token>`   |

```http
Authorization: Bearer <access_token>
```

Access tokens are obtained from:

- `POST /v1/register`
- `POST /v1/login`
- `POST /v1/refresh`

If the token is missing, invalid, or expired, the server responds with **401** and a JSON error body.

### Admin API

Routes under `/v1/admin/*` require:

| Header           | Value                 |
| ---------------- | --------------------- |
| `X-Admin-Token`  | Shared admin secret   |

```http
X-Admin-Token: <server admin token>
```

They do not use Bearer session auth.

---

## Error responses

Errors use the same JSON shape across the API:

```json
{
  "error": {
    "code": <integer>,
    "message": "<string>"
  }
}
```

`code` is a server `ErrorCode` (see `lib/vox_common/types.hpp`).

**Error object** (`application/json`, used in 4xx/5xx responses that return JSON)

| Field       | Type    | Description                          |
| ----------- | ------- | ------------------------------------ |
| `error`     | object  | Wrapper                              |
| `error.code` | integer | Numeric `ErrorCode`                  |
| `error.message` | string | Human-readable message            |

Typical HTTP meanings:

| HTTP | Meaning                                 |
| ---- | --------------------------------------- |
| 400  | Invalid argument / malformed request    |
| 401  | Unauthorized / expired token            |
| 403  | Forbidden                               |
| 404  | Not found                               |
| 409  | Conflict / version mismatch / duplicate |
| 413  | Quota exceeded                          |
| 429  | Rate limited                            |
| 500  | Internal error                          |
| 503  | Queue full / temporary overload         |

### Rate limiting

When rate limiting is enabled in server config, exceeded limits produce **HTTP 429** with the usual JSON error body, **`error.code` = `8`** (`ErrorCode::kRateLimited`), and **`error.message`** = `"Too many requests"`.

| Limiter | Scope | Routes | Key |
|--------|--------|--------|-----|
| **Auth** | Per **client IP** (from the TCP connection; empty IP is bucketed as `unknown`) | `POST /v1/register`, `POST /v1/login`, `POST /v1/refresh` | Active when `auth_rate_limit_max` is non-zero. Window: `auth_rate_limit_window_seconds`. |
| **Account** | Per **authenticated user** (`user_id` from the session) | `POST /v1/messages/send`, `PUT /v1/attachments/{attachment_id}/chunk?...` | Active when `account_rate_limit_max` is non-zero. Window: `account_rate_limit_window_seconds`. |

Built-in defaults are in `lib/vox_common/config.hpp`; production overrides often set `auth_rate_limit_*` in `deploy/vox.conf`. Account limiting stays off until `account_rate_limit_max` is set above zero.

**429 example**

```json
{
  "error": {
    "code": 8,
    "message": "Too many requests"
  }
}
```

---

## Public endpoints (no Bearer)

### `GET /v1/health`

Liveness probe.

**Response `200`** (`application/json`)

| Field    | Type   | Description      |
| -------- | ------ | ---------------- |
| `status` | string | Always `"ok"` when healthy |

**200**

```json
{ "status": "ok" }
```

Example:

```bash
curl -sS https://<host>/v1/health
```

Use this for load balancers, uptime checks, or a quick sanity check. Auth routes such as `POST /v1/login` are **POST-only**; opening them in a tab issues **GET** and will not hit the login handler (you may see **404** for unknown paths without Bearer, not **401**).

---

### `POST /v1/register`

Create a new user and create the first device session.

**Request body** (`application/json`)

| Field                     | Type   | Description                                                 |
| ------------------------- | ------ | ----------------------------------------------------------- |
| `username`                | string | Unique within one server                                    |
| `password_derived_value`  | string | Client-derived secret for auth                              |
| `device_id`               | string | Client-generated stable device id                           |
| `device_label`            | string | Optional human-readable device name                         |
| `identity_key_public`     | string | Public identity key for this device                         |
| `signed_prekey_public`    | string | Public signed prekey                                        |
| `signed_prekey_signature` | string | Signature over the signed prekey by the identity key        |
| `wrapped_sync_key`        | string | Password-wrapped sync master key blob                       |
| `sync_wrap_salt`          | string | Salt for deriving the key used to unwrap `wrapped_sync_key` |
| `sync_wrap_params`        | object | KDF parameters used for wrapping                            |

**Response `200`** (`application/json`)

| Field              | Type    | Description                           |
| ------------------ | ------- | ------------------------------------- |
| `user_id`          | string  | User id                               |
| `access_token`     | string  | Bearer access token                   |
| `refresh_token`    | string  | Refresh token bound to device         |
| `device_status`    | string  | e.g. `"created"`                      |
| `sync_key_version` | integer | User sync-key bundle version          |

**200**

```json
{
  "user_id": "usr_...",
  "access_token": "acc_...",
  "refresh_token": "ref_...",
  "device_status": "created",
  "sync_key_version": 1
}
```

**Notes**

- `wrapped_sync_key` is created by the client from a random sync master key.
- The server stores the wrapped blob and metadata, but cannot decrypt it.
- This enables a new device to recover the contact-sync key using the user's password without the server ever learning the plaintext key.

---

### `POST /v1/login`

Authenticate a user and bind the session to a device.

**Request body** (`application/json`)

| Field                     | Type   | Description                                       |
| ------------------------- | ------ | ------------------------------------------------- |
| `username`                | string |                                                   |
| `password_derived_value`  | string |                                                   |
| `device_id`               | string |                                                   |
| `device_label`            | string | Optional; used when auto-registering a new device |
| `identity_key_public`     | string | Required if `device_id` is new                    |
| `signed_prekey_public`    | string | Required if `device_id` is new                    |
| `signed_prekey_signature` | string | Required if `device_id` is new                    |

**Response `200`** (`application/json`)

| Field              | Type    | Description |
| ------------------ | ------- | ----------- |
| `user_id`          | string  |             |
| `access_token`     | string  |             |
| `refresh_token`    | string  |             |
| `device_status`    | string  | `"existing"` or `"created"` when a new device is registered during login |
| `sync_key_version` | integer |             |

**Example** request bodies:

**Behavior**

- Device rows are keyed by **`(user_id, device_id)`**. The same `device_id` string may be registered independently for different accounts (e.g. several users on one physical device). Uniqueness is per user, not globally.
- If `device_id` already exists **for this user**, the server opens a session for the existing device.
- If `device_id` is new **for this user** **and** the device public-key fields are present, the server creates a new device and opens a session for it.
- If `device_id` is new for this user and the public-key fields are missing, return **400**.

**200**

```json
{
  "user_id": "usr_...",
  "access_token": "acc_...",
  "refresh_token": "ref_...",
  "device_status": "existing",
  "sync_key_version": 1
}
```

or:

```json
{
  "user_id": "usr_...",
  "access_token": "acc_...",
  "refresh_token": "ref_...",
  "device_status": "created",
  "sync_key_version": 1
}
```

---

### `POST /v1/refresh`

**Request body** (`application/json`)

| Field            | Type   | Description        |
| ---------------- | ------ | ------------------ |
| `refresh_token`  | string | Valid refresh token |
| `device_id`      | string | Must match token’s device |

**Body**

```json
{
  "refresh_token": "ref_...",
  "device_id": "dev_..."
}
```

**Response `200`** (`application/json`)

| Field           | Type   | Description        |
| --------------- | ------ | ------------------ |
| `access_token`  | string | New access token   |
| `refresh_token` | string | Rotated refresh token |

**200**

```json
{
  "access_token": "acc_...",
  "refresh_token": "ref_..."
}
```

---

## Authenticated endpoints (Bearer required)

Unless stated otherwise, all endpoints below require `Authorization: Bearer`.

---

## Account and device management

### `GET /v1/me`

Return the current user and session device.

**Response `200`** (`application/json`)

| Field                 | Type    | Description                    |
| --------------------- | ------- | ------------------------------ |
| `user_id`             | string  | Authenticated user id          |
| `username`            | string  | Login name                     |
| `current_device_id`   | string  | Device id for this session     |
| `sync_key_version`    | integer | Current sync-key bundle version |

**200**

```json
{
  "user_id": "usr_...",
  "username": "alice",
  "current_device_id": "dev_phone",
  "sync_key_version": 1
}
```

---

### `POST /v1/logout`

Revoke the current session.

**Request body:** empty JSON object `{}` is accepted.

**Response `200`** (`application/json`): empty object.

**200**

```json
{}
```

---

### `POST /v1/account/change-password`

Change the account password and upload a re-wrapped sync key bundle.

**Request body** (`application/json`)

| Field                           | Type   | Description                                |
| ------------------------------- | ------ | ------------------------------------------ |
| `current_password_derived_value`| string | Proves knowledge of current password       |
| `new_password_derived_value`    | string | New verifier input (client-side KDF output) |
| `wrapped_sync_key`              | string | Re-wrapped sync master key blob            |
| `sync_wrap_salt`                | string | Salt for unwrapping                        |
| `sync_wrap_params`              | object | KDF parameters (e.g. Argon2id settings)    |

**Body**

```json
{
  "current_password_derived_value": "...",
  "new_password_derived_value": "...",
  "wrapped_sync_key": "...",
  "sync_wrap_salt": "...",
  "sync_wrap_params": {
    "algorithm": "argon2id",
    "memory_kib": 65536,
    "iterations": 3,
    "parallelism": 1
  }
}
```

**Response `200`** (`application/json`)

| Field              | Type    | Description                |
| ------------------ | ------- | -------------------------- |
| `sync_key_version` | integer | New bundle version after change |

**200**

```json
{
  "sync_key_version": 2
}
```

**Notes**

- The sync master key does not need to change on password change.
- The client may keep the same sync master key and upload a newly wrapped blob derived from the new password.

---

### `GET /v1/me/devices`

List the user's own devices.

**Response `200`** (`application/json`)

| Field      | Type  | Description |
| ---------- | ----- | ----------- |
| `devices`  | array | List of device objects |

Each element of `devices`:

| Field           | Type    | Description |
| --------------- | ------- | ----------- |
| `device_id`     | string  |             |
| `device_label`  | string  |             |
| `created_at`    | integer | Unix seconds |
| `last_seen_at`  | integer | Unix seconds |
| `is_current`    | boolean | `true` if this is the session device |
| `is_revoked`    | boolean |             |

**200**

```json
{
  "devices": [
    {
      "device_id": "dev_phone",
      "device_label": "Pixel 9",
      "created_at": 1710000000,
      "last_seen_at": 1710000500,
      "is_current": true,
      "is_revoked": false
    }
  ]
}
```

---

### `DELETE /v1/me/devices/{device_id}`

Revoke a device owned by the current user.

**Path parameters**

| Name        | Type   | Description                          |
| ----------- | ------ | ------------------------------------ |
| `device_id` | string | Target device (cannot be current device) |

**Response `200`** (`application/json`): empty object.

**200**

```json
{}
```

**Behavior**

- Revokes future sessions for the device.
- Prevents the device from receiving new envelopes.
- Removes or invalidates the device's published prekeys.
- Does not decrypt or expose any historical ciphertext.

---

## Discovery and user directory

The directory is intentionally minimal. It exists only to help a user discover another account on the same server and bootstrap E2EE sessions to that user's active devices.

The directory may expose:

- `user_id`
- `username` (canonical server-side login name as stored at registration; case-insensitive uniqueness)
- active device ids
- optional device labels
- public prekey bundle material

It must not expose:

- private contact aliases or notes
- per-user block lists
- local trust annotations
- decrypted contact-book contents

---

### `GET /v1/users/by-username/{username}`

Resolve one username to a user id.

**Path parameters**

| Name       | Type   | Description   |
| ---------- | ------ | ------------- |
| `username` | string | Username to resolve (matching is **case-insensitive**; response returns the **canonical** stored spelling) |

**Response `200`** (`application/json`)

| Field       | Type   | Description |
| ----------- | ------ | ----------- |
| `user_id`   | string |             |
| `username`  | string | Canonical username from the user row |

**200**

```json
{
  "user_id": "usr_bob",
  "username": "bob"
}
```

---

### `GET /v1/users/search?q=<query>&limit=<n>`

Search users on the current server.

**Query parameters**

| Name    | Type    | Required | Description |
| ------- | ------- | -------- | ----------- |
| `q`     | string  | no       | Prefix / query string (server-defined matching) |
| `limit` | integer | no       | Max results; server caps (e.g. 50) |

**Response `200`** (`application/json`)

| Field   | Type  | Description |
| ------- | ----- | ----------- |
| `users` | array | Objects with `user_id`, `username` |

**200**

```json
{
  "users": [
    {
      "user_id": "usr_bob",
      "username": "bob"
    },
    {
      "user_id": "usr_bobby",
      "username": "bobby"
    }
  ]
}
```

---

### `GET /v1/users?ids=<id1,id2,...>`

Batch-resolve public profiles by user id (avoids per-user `GET /v1/users/{user_id}` when rendering lists).

**Query parameters**

| Name | Type   | Required | Description |
| ---- | ------ | -------- | ----------- |
| `ids` | string | no       | Comma-separated `user_id` values. Multiple `ids=` query pairs are merged. Max **100** ids per request; exceeding returns **400**. |

**Response `200`** (`application/json`)

| Field   | Type  | Description |
| ------- | ----- | ----------- |
| `users` | array | Objects with `user_id`, `username` for **non-disabled** users only; unknown or disabled ids are omitted |

**200**

```json
{
  "users": [
    { "user_id": "usr_alice", "username": "alice" },
    { "user_id": "usr_bob", "username": "bob" }
  ]
}
```

---

### `GET /v1/users/{user_id}`

Get minimal public profile data for one user.

**Path parameters**

| Name      | Type   | Description |
| --------- | ------ | ----------- |
| `user_id` | string | User id     |

**Response `200`** (`application/json`)

| Field       | Type   | Description |
| ----------- | ------ | ----------- |
| `user_id`   | string |             |
| `username`  | string | Canonical username as stored server-side (authoritative spelling) |

**200**

```json
{
  "user_id": "usr_bob",
  "username": "bob"
}
```

---

### `GET /v1/users/{user_id}/devices`

List active devices for a user.

**Path parameters**

| Name      | Type   | Description |
| --------- | ------ | ----------- |
| `user_id` | string | Target user |

**Response `200`** (`application/json`)

| Field      | Type  | Description |
| ---------- | ----- | ----------- |
| `devices`  | array | Device summary objects |

Each element of `devices`:

| Field          | Type    | Description |
| -------------- | ------- | ----------- |
| `device_id`    | string  |             |
| `device_label` | string  |             |
| `is_revoked`   | boolean |             |
| `has_prekeys`  | boolean | Whether one-time prekeys are available |

**200**

```json
{
  "devices": [
    {
      "device_id": "dev_phone",
      "device_label": "Bob's phone",
      "is_revoked": false,
      "has_prekeys": true
    },
    {
      "device_id": "dev_tablet",
      "device_label": "Bob's tablet",
      "is_revoked": false,
      "has_prekeys": true
    }
  ]
}
```

---

### `GET /v1/users/{user_id}/prekey-bundles`

Fetch current public prekey bundles for all active devices of a user.

**Path parameters**

| Name      | Type   | Description |
| --------- | ------ | ----------- |
| `user_id` | string | Target user |

**Response `200`** (`application/json`)

| Field       | Type   | Description |
| ----------- | ------ | ----------- |
| `user_id`   | string |             |
| `username`  | string |             |
| `bundles`   | array  | One object per active device with keys |

Each element of `bundles`:

| Field                    | Type   | Required | Description |
| ------------------------ | ------ | -------- | ----------- |
| `device_id`              | string | yes      |             |
| `device_label`           | string | yes      |             |
| `identity_key_public`    | string | yes      |             |
| `signed_prekey_public`   | string | yes      |             |
| `signed_prekey_signature`| string | yes      |             |
| `one_time_prekey_public` | string | no     | Present when a one-time key is consumed |
| `one_time_prekey_id`     | string | no     |             |

**200**

```json
{
  "user_id": "usr_bob",
  "username": "bob",
  "bundles": [
    {
      "device_id": "dev_phone",
      "device_label": "Bob's phone",
      "identity_key_public": "...",
      "signed_prekey_public": "...",
      "signed_prekey_signature": "...",
      "one_time_prekey_public": "...",
      "one_time_prekey_id": "opk_1"
    },
    {
      "device_id": "dev_tablet",
      "device_label": "Bob's tablet",
      "identity_key_public": "...",
      "signed_prekey_public": "...",
      "signed_prekey_signature": "...",
      "one_time_prekey_public": "...",
      "one_time_prekey_id": "opk_2"
    }
  ]
}
```

This is the preferred endpoint for multi-device DM bootstrap.

---

## Device key publication and rotation

Device endpoints are scoped by **`user_id` + `device_id`**. The preferred path form is:

`/v1/users/{user_id}/devices/{device_id}/…`

Legacy paths `/v1/devices/{device_id}/…` still work when the client targets **its own** session device (same `device_id` as the Bearer session). For fetching another user’s prekey bundle, use the user-scoped URL below, or the legacy URL only when that `device_id` is unambiguous on the server.

### `POST /v1/users/{user_id}/devices/{device_id}/prekeys`

Upload one-time prekeys for the authenticated device.

**Path parameters**

| Name        | Type   | Description |
| ----------- | ------ | ----------- |
| `user_id`   | string | Must equal session `user_id` |
| `device_id` | string | Must equal session `device_id` |

Same request body as `POST /v1/devices/{device_id}/prekeys`.

---

### `POST /v1/devices/{device_id}/prekeys`

Upload one-time prekeys for the authenticated device (legacy; same as above with `user_id` implied by the session).

**Path parameters**

| Name        | Type   | Description |
| ----------- | ------ | ----------- |
| `device_id` | string | Must equal session `device_id` |

**Request body** (`application/json`)

| Field      | Type  | Description |
| ---------- | ----- | ----------- |
| `prekeys`  | array | List of `{ prekey_id, prekey_public }` |

Each element of `prekeys`:

| Field           | Type   | Description |
| --------------- | ------ | ----------- |
| `prekey_id`     | string |             |
| `prekey_public` | string |             |

**Response `200`:** empty JSON object.

**Body**

```json
{
  "prekeys": [
    {
      "prekey_id": "opk_123",
      "prekey_public": "..."
    }
  ]
}
```

**200**

```json
{}
```

The path `device_id` must equal the session's device id.

---

### `PUT /v1/users/{user_id}/devices/{device_id}/signed-prekey`

Rotate the signed prekey for the authenticated device. Path `user_id` and `device_id` must match the session.

Same body as `PUT /v1/devices/{device_id}/signed-prekey`.

---

### `PUT /v1/devices/{device_id}/signed-prekey`

Rotate the signed prekey for the authenticated device.

**Path parameters**

| Name        | Type   | Description |
| ----------- | ------ | ----------- |
| `device_id` | string | Must equal session `device_id` |

**Request body** (`application/json`)

| Field                      | Type   | Description |
| -------------------------- | ------ | ----------- |
| `signed_prekey_public`     | string |             |
| `signed_prekey_signature`  | string |             |

**Response `200`:** empty JSON object.

**Body**

```json
{
  "signed_prekey_public": "...",
  "signed_prekey_signature": "..."
}
```

**200**

```json
{}
```

The path `device_id` must equal the session's device id.

---

### `GET /v1/users/{user_id}/devices/{device_id}/prekey-bundle`

Fetch the public bundle for a specific device row (**preferred**).

**Path parameters**

| Name        | Type   | Description |
| ----------- | ------ | ----------- |
| `user_id`   | string | Owner of the device |
| `device_id` | string | Client device id |

**Response `200`** (`application/json`)

| Field                      | Type   | Required | Description |
| -------------------------- | ------ | -------- | ----------- |
| `user_id`                  | string | yes      | Same as path |
| `device_id`                | string | yes      |             |
| `identity_key_public`      | string | yes      |             |
| `signed_prekey_public`     | string | yes      |             |
| `signed_prekey_signature`  | string | yes      |             |
| `one_time_prekey_public`   | string | no       |             |
| `one_time_prekey_id`       | string | no       |             |

---

### `GET /v1/devices/{device_id}/prekey-bundle`

Fetch the public bundle for one device.

**Path parameters**

| Name        | Type   | Description |
| ----------- | ------ | ----------- |
| `device_id` | string | Target device id |

If more than one account has registered the same `device_id` string, returns **400** with a message to use `GET /v1/users/{user_id}/devices/{device_id}/prekey-bundle`.

**Response `200`** (`application/json`)

| Field                      | Type   | Required | Description |
| -------------------------- | ------ | -------- | ----------- |
| `user_id`                  | string | yes      | Owner of the device row |
| `device_id`                | string | yes      |             |
| `identity_key_public`      | string | yes      |             |
| `signed_prekey_public`     | string | yes      |             |
| `signed_prekey_signature`  | string | yes      |             |
| `one_time_prekey_public`   | string | no       |             |
| `one_time_prekey_id`       | string | no       |             |

**200**

```json
{
  "user_id": "usr_bob",
  "device_id": "dev_phone",
  "identity_key_public": "...",
  "signed_prekey_public": "...",
  "signed_prekey_signature": "...",
  "one_time_prekey_public": "...",
  "one_time_prekey_id": "opk_123"
}
```

This route is useful when the client already knows the exact target device id and it is unique for that string on the server.

---

## Encrypted client-state synchronization between the user's own devices

This part of the API is **opaque to the server**. The server stores encrypted records and sync metadata only.

Typical collections:

- `contacts`
- `contact_trust`
- `blocklist`
- `preferences`
- `drafts`

The server stores:

- collection name
- record id
- ciphertext
- content hash
- version
- server timestamps
- tombstone flag

The server does **not** understand the plaintext schema inside `ciphertext`.

---

### `GET /v1/sync/key-bundle`

Fetch the password-wrapped sync-key metadata for this user.

**Response `200`** (`application/json`)

| Field              | Type   | Description |
| ------------------ | ------ | ----------- |
| `sync_key_version` | integer |            |
| `wrapped_sync_key` | string | Opaque blob |
| `sync_wrap_salt`   | string |             |
| `sync_wrap_params` | object | Parsed JSON KDF parameters |

**200**

```json
{
  "sync_key_version": 1,
  "wrapped_sync_key": "...",
  "sync_wrap_salt": "...",
  "sync_wrap_params": {
    "algorithm": "argon2id",
    "memory_kib": 65536,
    "iterations": 3,
    "parallelism": 1
  }
}
```

A new device uses this response, plus the user's password, to derive the unwrap key locally and recover the sync master key.

---

### `PUT /v1/sync/key-bundle`

Replace the wrapped sync-key metadata.

Use this after password change or after rotating the sync master key.

**Request body** (`application/json`)

| Field              | Type   | Description |
| ------------------ | ------ | ----------- |
| `wrapped_sync_key` | string |             |
| `sync_wrap_salt`   | string |             |
| `sync_wrap_params` | object | KDF parameters JSON |

**Response `200`** (`application/json`)

| Field              | Type    | Description |
| ------------------ | ------- | ----------- |
| `sync_key_version` | integer | New version after replace |

**Body**

```json
{
  "wrapped_sync_key": "...",
  "sync_wrap_salt": "...",
  "sync_wrap_params": {
    "algorithm": "argon2id",
    "memory_kib": 65536,
    "iterations": 3,
    "parallelism": 1
  }
}
```

**200**

```json
{
  "sync_key_version": 2
}
```

---

### `GET /v1/sync/changes?collection=<name>&cursor=<opaque>&limit=<n>`

Read encrypted sync changes for one collection.

**Query parameters**

| Name         | Type   | Required | Description |
| ------------ | ------ | -------- | ----------- |
| `collection` | string | yes      | Collection name (e.g. `contacts`) |
| `cursor`     | string | no       | Opaque resume token |
| `limit`      | integer | no      | Page size (server default if omitted) |

**Response `200`** (`application/json`)

| Field          | Type    | Description |
| -------------- | ------- | ----------- |
| `collection`   | string  | Same as requested |
| `changes`      | array   | Change records |
| `next_cursor`  | string  | Opaque; empty when no more pages |
| `has_more`     | boolean | Whether more pages exist |

Each element of `changes`:

| Field               | Type    | Description |
| ------------------- | ------- | ----------- |
| `record_id`         | string  |             |
| `ciphertext`        | string  | Opaque; empty if tombstone |
| `content_hash`      | string  |             |
| `version`           | integer |             |
| `server_updated_at` | integer | Unix seconds |
| `deleted`           | boolean | Tombstone flag |

**200**

```json
{
  "collection": "contacts",
  "changes": [
    {
      "record_id": "contact_usr_bob",
      "ciphertext": "...",
      "content_hash": "...",
      "version": 7,
      "server_updated_at": 1710001000,
      "deleted": false
    },
    {
      "record_id": "contact_usr_eve",
      "ciphertext": "",
      "content_hash": "",
      "version": 3,
      "server_updated_at": 1710001010,
      "deleted": true
    }
  ],
  "next_cursor": "opaque_cursor",
  "has_more": false
}
```

**Behavior**

- If `cursor` is omitted, the server returns the current collection snapshot as a stream of changes.
- If `cursor` is present, the server returns only changes after that cursor.
- `next_cursor` is opaque and stable for resume.

---

### `PUT /v1/sync/records/{collection}/{record_id}`

Create or update one encrypted sync record.

**Path parameters**

| Name        | Type   | Description |
| ----------- | ------ | ----------- |
| `collection`| string | Collection name |
| `record_id` | string | Record id within collection |

**Request body** (`application/json`)

| Field               | Type    | Description |
| ------------------- | ------- | ----------- |
| `device_id`         | string  | Must match session device |
| `ciphertext`        | string  | Opaque payload |
| `content_hash`      | string  | Client-computed hash |
| `base_version`      | integer | Expected current version (optimistic lock) |
| `client_updated_at` | integer | Client timestamp (Unix seconds) |

**Response `200`** (`application/json`)

| Field               | Type    | Description |
| ------------------- | ------- | ----------- |
| `record_id`         | string  |             |
| `version`           | integer | New record version |
| `server_updated_at` | integer | Unix seconds |

**Body**

```json
{
  "device_id": "dev_phone",
  "ciphertext": "...",
  "content_hash": "...",
  "base_version": 6,
  "client_updated_at": 1710002000
}
```

**200**

```json
{
  "record_id": "contact_usr_bob",
  "version": 7,
  "server_updated_at": 1710002001
}
```

**Conflict rule**

- If `base_version` does not match the current record version, return **409**.
- The client resolves the conflict by downloading the current encrypted record, decrypting locally, merging, and writing again.

---

### `DELETE /v1/sync/records/{collection}/{record_id}`

Tombstone one encrypted sync record.

**Path parameters**

| Name        | Type   | Description |
| ----------- | ------ | ----------- |
| `collection`| string | Collection name |
| `record_id` | string | Record id |

**Request body** (`application/json`)

| Field               | Type    | Description |
| ------------------- | ------- | ----------- |
| `device_id`         | string  | Must match session device |
| `base_version`      | integer | Expected current version |
| `client_updated_at` | integer | Client timestamp |

**Response `200`** (`application/json`)

| Field               | Type    | Description |
| ------------------- | ------- | ----------- |
| `record_id`         | string  |             |
| `version`           | integer | New version after tombstone |
| `server_updated_at` | integer | Unix seconds |
| `deleted`           | boolean | `true` |

**Body**

```json
{
  "device_id": "dev_phone",
  "base_version": 7,
  "client_updated_at": 1710003000
}
```

**200**

```json
{
  "record_id": "contact_usr_bob",
  "version": 8,
  "server_updated_at": 1710003001,
  "deleted": true
}
```

---

## Conversations

### `GET /v1/conversations`

List conversations for the current user.

**Response `200`** (`application/json`)

| Field            | Type  | Description |
| ---------------- | ----- | ----------- |
| `conversations`  | array | Summary objects |

Each element of `conversations`:

| Field                | Type            | Description |
| -------------------- | --------------- | ----------- |
| `conversation_id`    | string          |             |
| `type`               | integer         | `0` DM, `1` group, `2` channel |
| `created_by`         | string          | User id     |
| `created_by_username` | string or null | Canonical username for `created_by` (non-disabled); `null` if not available |
| `created_at`         | integer         | Unix seconds (conversation created) |
| `membership_version` | integer         | Bumps on membership changes |
| `last_activity_at`   | integer or null | Unix seconds of the latest stored envelope in this conversation (`MAX(server_timestamp)`); `null` if there are no envelopes yet |

**200**

```json
{
  "conversations": [
    {
      "conversation_id": "conv_1",
      "type": 0,
      "created_by": "usr_alice",
      "created_by_username": "alice",
      "created_at": 1710000000,
      "membership_version": 3,
      "last_activity_at": 1710000123
    }
  ]
}
```

`type` values:

- `0` = DM
- `1` = group
- `2` = channel

---

### `GET /v1/conversations/{conversation_id}`

Return one conversation with enough metadata for crypto and UI state.

**Path parameters**

| Name               | Type   | Description |
| ------------------ | ------ | ----------- |
| `conversation_id`  | string | Conversation id |

**Response `200`** (`application/json`) — shape depends on `type`.

Common fields:

| Field                | Type    | Description |
| -------------------- | ------- | ----------- |
| `conversation_id`    | string  |             |
| `type`               | integer | `0` DM, `1` group, `2` channel |
| `created_by`         | string  |             |
| `created_by_username` | string or null | Canonical username for `created_by` |
| `created_at`         | integer | Unix seconds |
| `membership_version` | integer |             |
| `my_role`            | string  | `"owner"`, `"admin"`, or `"member"` |

Additional fields for **channel** (`type == 2`) only:

| Field                   | Type   | Description |
| ----------------------- | ------ | ----------- |
| `title`                 | string | Channel title (may be empty) |
| `channel_post_policy`   | string | e.g. `admins_only` or policy serialized by server |

**200 (DM or group)** — `title` and `channel_post_policy` are **not** returned; only `my_role` among the extra fields below.

```json
{
  "conversation_id": "conv_1",
  "type": 1,
  "created_by": "usr_alice",
  "created_by_username": "alice",
  "created_at": 1710000000,
  "membership_version": 3,
  "my_role": "admin"
}
```

**200 (channel)** — includes optional channel metadata from server policy:

```json
{
  "conversation_id": "conv_channel",
  "type": 2,
  "created_by": "usr_alice",
  "created_by_username": "alice",
  "created_at": 1710000000,
  "membership_version": 8,
  "title": "Announcements",
  "my_role": "admin",
  "channel_post_policy": "admins_only"
}
```

---

### `GET /v1/conversations/{conversation_id}/members`

Return current membership.

**Path parameters**

| Name               | Type   | Description |
| ------------------ | ------ | ----------- |
| `conversation_id`  | string |             |

**Response `200`** (`application/json`) — one of the shapes below.

DM / group:

| Field                | Type   | Description |
| -------------------- | ------ | ----------- |
| `conversation_id`    | string |             |
| `membership_version` | integer |            |
| `members`            | array  | `{ user_id, username, role }` entries (`username` canonical or `null` if unknown/disabled) |

Channel (non-admin subscriber):

| Field                  | Type    | Description |
| ---------------------- | ------- | ----------- |
| `conversation_id`      | string  |             |
| `membership_version`   | integer |             |
| `admins`               | array   | Admin roster |
| `subscription_state`   | string  | e.g. `subscribed` |
| `member_count`         | integer | Approximate subscriber count |

Channel (admin):

| Field                | Type  | Description |
| -------------------- | ----- | ----------- |
| `admins`             | array | `{ user_id, username, role }` |
| `subscribers`        | array | Non-admin subscribers (`user_id`, `username`, `role`) |

**200 for DM / group**

```json
{
  "conversation_id": "conv_1",
  "membership_version": 3,
  "members": [
    {
      "user_id": "usr_alice",
      "username": "alice",
      "role": "owner"
    },
    {
      "user_id": "usr_bob",
      "username": "bob",
      "role": "member"
    }
  ]
}
```

**200 for channel, regular subscriber**

```json
{
  "conversation_id": "conv_channel",
  "membership_version": 8,
  "admins": [
    {
      "user_id": "usr_alice",
      "username": "alice",
      "role": "owner"
    }
  ],
  "subscription_state": "subscribed",
  "member_count": 523
}
```

**200 for channel, caller is admin**

```json
{
  "conversation_id": "conv_channel",
  "membership_version": 8,
  "admins": [
    {
      "user_id": "usr_alice",
      "username": "alice",
      "role": "owner"
    }
  ],
  "subscribers": [
    {
      "user_id": "usr_bob",
      "username": "bob",
      "role": "member"
    }
  ]
}
```

This preserves the requirement that channels are not a public roster feature while still letting the server manage subscriptions.

---

### `POST /v1/conversations`

Create a conversation.

**Request body** (`application/json`) — discriminated by `type`:

| `type` value | Fields | Description |
| ------------ | ------ | ----------- |
| `"dm"`       | `peer_user_id` (string) | Other user id for 1:1; may equal the caller’s `user_id` to create a **self DM** (notes / messages to yourself) |
| `"group"`    | `members` (array of user id strings) | Initial members besides creator |
| `"channel"`  | `admins`, `subscribers` (arrays of user id strings, optional) | Channel admins and subscribers |

**DM**

```json
{
  "type": "dm",
  "peer_user_id": "usr_bob"
}
```

**Self DM** — set `peer_user_id` to **your own** `user_id` (here the session user is `usr_alice`):

```json
{
  "type": "dm",
  "peer_user_id": "usr_alice"
}
```

The resulting DM has a single member (you). Use it like a notes / saved-messages thread.

**Group**

```json
{
  "type": "group",
  "members": ["usr_bob", "usr_carol"]
}
```

**Channel**

```json
{
  "type": "channel",
  "admins": ["usr_alice"],
  "subscribers": ["usr_bob", "usr_carol"]
}
```

**Response `200`** (`application/json`)

| Field              | Type   | Description |
| ------------------ | ------ | ----------- |
| `conversation_id`  | string | New conversation id |

**200**

```json
{
  "conversation_id": "conv_..."
}
```

---

### `POST /v1/conversations/{conversation_id}/members`

Add a member to a DM/group or add an admin/member where supported.

**Path parameters**

| Name               | Type   | Description |
| ------------------ | ------ | ----------- |
| `conversation_id`  | string |             |

**Request body** (`application/json`)

| Field      | Type   | Description |
| ---------- | ------ | ----------- |
| `user_id`  | string | User to add |
| `role`     | string | `"owner"`, `"admin"`, or `"member"` (default `"member"`) |

**Body**

```json
{
  "user_id": "usr_carol",
  "role": "member"
}
```

**200**

```json
{}
```

---

### `DELETE /v1/conversations/{conversation_id}/members/{user_id}`

Remove a member.

**Path parameters**

| Name               | Type   | Description |
| ------------------ | ------ | ----------- |
| `conversation_id`  | string |             |
| `user_id`          | string | Member to remove |

**Response `200`:** empty JSON object.

**200**

```json
{}
```

---

### `POST /v1/conversations/{conversation_id}/subscribe`

Subscribe the current user to a channel.

**Path parameters**

| Name               | Type   | Description |
| ------------------ | ------ | ----------- |
| `conversation_id`  | string | Channel id |

**Response `200`:** empty JSON object.

**200**

```json
{}
```

---

### `POST /v1/conversations/{conversation_id}/unsubscribe`

Unsubscribe the current user from a channel.

**Path parameters**

| Name               | Type   | Description |
| ------------------ | ------ | ----------- |
| `conversation_id`  | string | Channel id |

**Response `200`:** empty JSON object.

**200**

```json
{}
```

---

## Messaging

### `POST /v1/messages/send`

Send an encrypted envelope to a conversation.

The **sender** is always the authenticated session: the server stores `sender_user_id` and `sender_device_id` from the Bearer session (not from extra body fields).

**Request body** (`application/json`)

| Field            | Type    | Required | Description |
| ---------------- | ------- | -------- | ----------- |
| `device_id`      | string  | yes      | Must equal session device id |
| `conversation_id`| string | yes      |             |
| `ciphertext`     | string  | yes      | Encrypted payload |
| `envelope_id`    | string  | yes      | Client-generated id (dedup) |
| `envelope_type`  | integer | no       | Opaque to server |
| `ordering_epoch` | integer | no       | Optional ordering hint |

**Response `200`** (`application/json`)

| Field                 | Type    | Description |
| --------------------- | ------- | ----------- |
| `envelope_id`         | string  | Stored id (same as request if provided) |
| `server_timestamp`    | integer | Unix seconds |
| `delivered_to_count`  | integer | In-memory delivery targets notified |

**Body**

```json
{
  "device_id": "dev_phone",
  "conversation_id": "conv_1",
  "ciphertext": "...",
  "envelope_id": "env_123",
  "envelope_type": 0
}
```

**200**

```json
{
  "envelope_id": "env_123",
  "server_timestamp": 1710004000,
  "delivered_to_count": 2
}
```

**Server rule for channels**

- if the conversation is a channel with `channel_post_policy = admins_only`,
- the server must reject `POST /v1/messages/send` from non-admin senders with **403**.

---

### `POST /v1/messages/ack`

Acknowledge that the authenticated device received one envelope.

**Request body** (`application/json`)

| Field          | Type   | Description |
| -------------- | ------ | ----------- |
| `device_id`    | string | Must match session device |
| `envelope_id`  | string | Envelope to ack |

**Body**

```json
{
  "device_id": "dev_phone",
  "envelope_id": "env_123"
}
```

**200**

```json
{}
```

---

### `GET /v1/sync/pending`

Return pending envelopes for the authenticated device.

**Query parameters**

| Name    | Type    | Required | Description |
| ------- | ------- | -------- | ----------- |
| `limit` | integer | no       | Default 100 |
| `cursor`| string  | no       | Opaque; from previous `next_cursor` |

**Response `200`** (`application/json`)

| Field             | Type    | Description |
| ----------------- | ------- | ----------- |
| `envelopes`       | array   | Pending envelope objects |
| `next_cursor`     | string  | Opaque pagination |
| `has_more`        | boolean |             |

Each element of `envelopes`:

| Field               | Type    | Description |
| ------------------- | ------- | ----------- |
| `envelope_id`       | string  |             |
| `conversation_id`   | string  |             |
| `sender_user_id`    | string  | Account that sent the envelope |
| `sender_device_id`  | string  |             |
| `ciphertext`        | string  |             |
| `server_timestamp`  | integer |             |
| `envelope_type`     | integer |             |
| `ordering_epoch`    | integer | Optional |

**200**

```json
{
  "envelopes": [
    {
      "envelope_id": "env_123",
      "conversation_id": "conv_1",
      "sender_user_id": "usr_bob",
      "sender_device_id": "dev_remote",
      "ciphertext": "...",
      "server_timestamp": 1710004000,
      "envelope_type": 0
    }
  ],
  "next_cursor": "opaque_pending_cursor",
  "has_more": false
}
```

Each envelope object may include **`ordering_epoch`** (integer) when the server uses it for ordering.

---

### `GET /v1/conversations/{conversation_id}/envelopes`

Conversation history.

**Path parameters**

| Name               | Type   | Description |
| ------------------ | ------ | ----------- |
| `conversation_id`  | string |             |

**Query parameters**

| Name    | Type    | Required | Description |
| ------- | ------- | -------- | ----------- |
| `limit` | integer | no       | Default 100 |
| `cursor`| string  | no       | Opaque cursor pagination |
| `since` | integer | no       | Legacy: only when `cursor` empty; min `server_timestamp` |

**Response `200`** (`application/json`)

| Field         | Type    | Description |
| ------------- | ------- | ----------- |
| `envelopes`   | array   |             |
| `next_cursor` | string  | Empty in legacy `since` mode |
| `has_more`    | boolean |             |

Envelope object fields: same as in `GET /v1/sync/pending` list items (`ordering_epoch` optional).

Prefer **cursor** pagination. If **`cursor` is omitted** and **`since`** is set, the server uses legacy timestamp filtering (`next_cursor` / `has_more` not used for continuation in that mode).

**200**

```json
{
  "envelopes": [
    {
      "envelope_id": "env_120",
      "conversation_id": "conv_1",
      "sender_user_id": "usr_bob",
      "sender_device_id": "dev_remote",
      "ciphertext": "...",
      "server_timestamp": 1710003000,
      "envelope_type": 0
    }
  ],
  "next_cursor": "opaque_history_cursor",
  "has_more": false
}
```

Envelope objects may include **`ordering_epoch`** (integer) when present.

---

## Attachments

### `POST /v1/attachments/upload-init`

**Request body** (`application/json`)

| Field              | Type   | Description |
| ------------------ | ------ | ----------- |
| `conversation_id`  | string | Conversation the blob belongs to |
| `file_size`        | integer| Total size in bytes |
| `mime_hint`        | string | Optional MIME hint |

**Response `200`** (`application/json`)

| Field            | Type   | Description |
| ---------------- | ------ | ----------- |
| `attachment_id`  | string | Id for subsequent chunk/finalize/get |
| `blob_path`        | string | Server-side storage path / identifier |

**Body**

```json
{
  "conversation_id": "conv_1",
  "file_size": 123456,
  "mime_hint": "image/jpeg"
}
```

**200**

```json
{
  "attachment_id": "att_1",
  "blob_path": "/blob/att_1"
}
```

---

### `PUT /v1/attachments/{attachment_id}/chunk?offset=<bytes>`

Upload raw ciphertext bytes for one attachment chunk.

**Path parameters**

| Name            | Type   | Description |
| --------------- | ------ | ----------- |
| `attachment_id` | string | From `upload-init` |

**Query parameters**

| Name     | Type    | Required | Description |
| -------- | ------- | -------- | ----------- |
| `offset` | integer | yes      | Byte offset in the blob |

**Request body:** raw bytes (`application/octet-stream` or any non-JSON body; not JSON).

**Response `200`** (`application/json`): empty object.

**200**

```json
{}
```

---

### `POST /v1/attachments/{attachment_id}/finalize`

**Path parameters**

| Name            | Type   | Description |
| --------------- | ------ | ----------- |
| `attachment_id` | string |             |

**Request body** (`application/json`)

| Field             | Type   | Description |
| ----------------- | ------ | ----------- |
| `ciphertext_hash` | string | Integrity check (e.g. `sha256:...`) |

**Body**

```json
{
  "ciphertext_hash": "sha256:..."
}
```

**200**

```json
{}
```

---

### `GET /v1/attachments/{attachment_id}`

Download ciphertext bytes.

**Path parameters**

| Name            | Type   | Description |
| --------------- | ------ | ----------- |
| `attachment_id` | string |             |

**Response `200`**

| Part        | Description |
| ----------- | ----------- |
| Header      | `Content-Type: application/octet-stream` |
| Body        | Raw encrypted bytes |

**Response**

- `Content-Type: application/octet-stream`
- Body is raw encrypted data

---

## WebSocket

### `GET /v1/ws`

Upgrade to WebSocket on the same host as HTTP.

### Authentication options

**Headers (preferred upgrade)**

| Header            | Value                    | Required |
| ----------------- | ------------------------ | -------- |
| `Authorization`   | `Bearer <access_token>`  | one of auth methods |

**Deferred auth** (first text frame within ~5 s after upgrade if no Bearer on upgrade)

**Request body** (first WebSocket text frame, JSON)

| Field            | Type   | Description |
| ---------------- | ------ | ----------- |
| `type`           | string | Must be `"auth"` |
| `access_token`   | string | Same as HTTP Bearer token |

Preferred:

```http
Authorization: Bearer <access_token>
```

Fallback for environments that cannot set upgrade headers:

1. complete the WebSocket upgrade without auth;
2. send the first frame within 5 seconds:

```json
{
  "type": "auth",
  "access_token": "acc_..."
}
```

If authentication fails, the server closes the socket.

### Server-pushed events

Each message is one JSON object per line (text frame). **`type`** discriminates the schema.

#### Envelope

| Field               | Type    | Description |
| ------------------- | ------- | ----------- |
| `type`              | string  | `"envelope"` |
| `envelope_id`       | string  |             |
| `conversation_id`   | string  |             |
| `sender_user_id`    | string  |             |
| `sender_device_id`  | string  |             |
| `ciphertext`        | string  |             |
| `server_timestamp`  | integer | Unix seconds |
| `envelope_type`     | integer |             |
| `ordering_epoch`    | integer | Optional |

```json
{
  "type": "envelope",
  "envelope_id": "env_123",
  "conversation_id": "conv_1",
  "sender_user_id": "usr_bob",
  "sender_device_id": "dev_remote",
  "ciphertext": "...",
  "server_timestamp": 1710004000,
  "envelope_type": 0
}
```

#### Conversation membership change

| Field                | Type    | Description |
| -------------------- | ------- | ----------- |
| `type`               | string  | `"conversation_membership_changed"` |
| `conversation_id`  | string  |             |
| `membership_version` | integer |             |

```json
{
  "type": "conversation_membership_changed",
  "conversation_id": "conv_1",
  "membership_version": 4
}
```

#### User-device directory change

| Field      | Type   | Description |
| ---------- | ------ | ----------- |
| `type`     | string | `"user_devices_changed"` |
| `user_id`  | string | User whose directory changed |

```json
{
  "type": "user_devices_changed",
  "user_id": "usr_bob"
}
```

#### Encrypted sync-record change

| Field        | Type    | Description |
| ------------ | ------- | ----------- |
| `type`       | string  | `"sync_record_changed"` |
| `collection` | string | Sync collection |
| `record_id`  | string  |             |
| `version`    | integer | New version |

```json
{
  "type": "sync_record_changed",
  "collection": "contacts",
  "record_id": "contact_usr_bob",
  "version": 8
}
```

---

## Admin

### `GET /v1/admin/stats`

**Headers:** `X-Admin-Token` (see [Admin API](#admin-api)).

**Response `200`** (`application/json`)

| Field                   | Type    | Description |
| ----------------------- | ------- | ----------- |
| `user_count`            | integer |             |
| `device_count`          | integer |             |
| `active_session_count`  | integer |             |
| `conversation_count`    | integer |             |
| `pending_envelope_count`| integer |             |
| `total_storage_bytes`   | integer | Attachment storage |

**200**

```json
{
  "user_count": 100,
  "device_count": 180,
  "active_session_count": 91,
  "conversation_count": 340,
  "pending_envelope_count": 42,
  "total_storage_bytes": 123456789
}
```

---

### `DELETE /v1/admin/users/{user_id}`

Delete a user by id.

**Headers:** `X-Admin-Token`.

**Path parameters**

| Name      | Type   | Description |
| --------- | ------ | ----------- |
| `user_id` | string | User to delete |

**Response `200`:** empty JSON object.

**200**

```json
{}
```

---

## Not found

Any other method/path returns **404** (JSON body may be empty for simple 404 responses from the HTTP stack).
