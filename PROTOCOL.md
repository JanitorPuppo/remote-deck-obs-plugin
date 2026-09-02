# Remote Deck OBS control protocol

Versioned JSON **text frames** over WebSocket between this plugin and Remote Deck.

Command direction is always **upstream → plugin → OBS**. The plugin dials out. Producers never receive an OBS host, port, protocol, or obs-websocket password.

Protocol version: **1**.

## Envelope

Every frame is a single JSON object:

```json
{
  "v": 1,
  "type": "hello",
  "id": "optional-correlation-id",
  "payload": {}
}
```

| Field | Required | Description |
| --- | --- | --- |
| `v` | no | Protocol version. Defaults to `1` if omitted. |
| `type` | yes | Method name. |
| `id` | no | Correlation id. Replies and errors echo it when present. |
| `payload` | no | Method-specific object. Defaults to `{}`. |

Unknown `type` values MUST be answered with an `error` frame (`code`: `unknown_method`). The connection stays open (forward-compatible).

Invalid JSON is answered with `error` / `protocol_error`.

## Transport and auth

Auth is **not** part of the JSON body. See [docs/REMOTE-DECK-CONTRACT.md](docs/REMOTE-DECK-CONTRACT.md). TLS (`wss`) is required except for localhost. The plugin sends:

```
Authorization: Bearer <studio-plugin-token>
```

## Plugin → upstream

### `hello`

Sent immediately after the socket opens.

```json
{
  "v": 1,
  "type": "hello",
  "payload": {
    "protocolVersion": 1,
    "pluginVersion": "1.0.0",
    "machineLabel": "STREAM-PC",
    "instanceId": "123e4567-e89b-12d3-a456-426614174abc"
  }
}
```

`instanceId` is the plugin-persisted UUID so the same OBS install rebinds the same studio setup.

### `inputs`

Full audio mixer snapshot. Sent after `hello`, after `inputs.get`, and when the source list changes.

```json
{
  "v": 1,
  "type": "inputs",
  "id": "optional-echo-of-inputs.get",
  "payload": {
    "inputs": [
      {
        "id": "a1b2c3d4-5678-90ab-cdef-1234567890ab",
        "name": "Mic/Aux",
        "muted": false,
        "volumeDb": -6.0
      }
    ]
  }
}
```

- `id` is the OBS source UUID.
- `name` is the mixer display name. Mute/volume commands match on `name` (and `id` when provided).
- `volumeDb` is OBS amplitude dB (`obs_mul_to_db` / `obs_db_to_mul`). `0` is unity. Values at or below `-100` are silence.

### `state`

One input changed in OBS (local mute, volume, or rename). Sent so an editor stays in sync if someone mutes at the mixer.

```json
{
  "v": 1,
  "type": "state",
  "payload": {
    "id": "a1b2c3d4-5678-90ab-cdef-1234567890ab",
    "name": "Mic/Aux",
    "muted": true,
    "volumeDb": -6.0
  }
}
```

### `pong`

Reply to `ping`. Echoes `id` when present.

### `error`

```json
{
  "v": 1,
  "type": "error",
  "id": "optional",
  "payload": {
    "code": "input_not_found",
    "message": "Audio input not found"
  }
}
```

| `code` | When |
| --- | --- |
| `unknown_method` | `type` is not recognized |
| `invalid_payload` | required fields missing |
| `input_not_found` | no matching audio input |
| `protocol_error` | frame is not valid JSON / envelope |

## Upstream → plugin

### `input.mute`

```json
{
  "v": 1,
  "type": "input.mute",
  "id": "optional",
  "payload": {
    "name": "Mic/Aux",
    "muted": true
  }
}
```

`id` is optional and preferred when present (disambiguates duplicate names).

### `input.volume`

```json
{
  "v": 1,
  "type": "input.volume",
  "payload": {
    "name": "Mic/Aux",
    "volumeDb": -12.5
  }
}
```

`volumeDb` is clamped to `[-100, 26]`.

### `inputs.get`

Request a fresh `inputs` snapshot. The plugin replies with `inputs` and echoes `id`.

```json
{ "v": 1, "type": "inputs.get", "id": "req-1" }
```

### `ping`

Plugin replies with `pong`.

### `error`

Informational. The plugin does not disconnect.

## Out of scope for v1

Scene switching, source visibility, filters, and raw obs-websocket proxying.
