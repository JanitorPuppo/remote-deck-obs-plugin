# Protocol fixture

A tiny Node WebSocket server that speaks [PROTOCOL.md](../PROTOCOL.md). The plugin itself talks only to Remote Deck; this process is for inspecting frames without a full OBS debug loop.

```powershell
npm install
npm start
```

Listens on `ws://127.0.0.1:8787`. Optional `--token test-token` requires `Authorization: Bearer test-token`.

Stdin commands (type into the fixture process):

| Command | Sends |
| --- | --- |
| `mute Mic/Aux true` | `input.mute` |
| `volume Mic/Aux -12` | `input.volume` |
| `list` | `inputs.get` |
| `ping` | `ping` |
| `raw {"v":1,"type":"ping"}` | verbatim frame |

The fixture prints every inbound frame (`hello`, `inputs`, `state`, `error`). `npm test` checks envelope shapes without OBS.
