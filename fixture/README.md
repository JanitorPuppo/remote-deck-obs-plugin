# Test server

A tiny stand-in for Remote Deck. Use it to watch plugin messages without opening OBS. Streamers do not need this.

```
npm install
npm start
```

It listens on `ws://127.0.0.1:8787`. Add `--token test-token` if you want it to require `Authorization: Bearer test-token`.

Type commands into that window:

| You type | What it sends |
| --- | --- |
| `mute Mic/Aux true` | mute |
| `volume Mic/Aux -12` | volume |
| `list` | ask for the mixer list |
| `ping` | ping |
| `raw {"v":1,"type":"ping"}` | that JSON as-is |

Inbound messages print in the same window. `npm test` checks message shapes and does not need OBS.
