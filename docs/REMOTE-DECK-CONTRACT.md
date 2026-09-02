# Remote Deck official-mode contract

The plugin's **Authenticate with Remote Deck** button runs a device-authorization flow. The streamer only names this machine. Tokens are never pasted by hand.

Implemented in the `remote-deck` API (not this repo):

## Device authorization

| Step | Request |
| --- | --- |
| Start | `POST {apiBase}/public/plugin-auth/v1/device` `{ machineLabel, instanceId, clientId: "obs-remote-deck", clientVersion }` returns `{ deviceCode, userCode, verificationUri, pollToken, … }` |
| Browser | Opens `verificationUri` (`/plugin/authorize/:deviceCode`). The plugin displays `userCode` (not `deviceCode`). The user types that code on the authorize page, then approves a studio. |
| Poll | `POST {apiBase}/public/plugin-auth/v1/token` `{ deviceCode, pollToken }` until `{ status: "complete", studioPluginToken, apiBase, wssUrl, studioId, studioName }` |

Default `apiBase` is `https://remotedeck.gg`. Public/release builds always use that. A **local-dev-only** compile (`ENABLE_REMOTE_DECK_LOCAL_DEV`, preset `windows-x64-dev`) adds a **Local Remote Deck URL** field (default `http://localhost:3000`) so you can authenticate against a hosted API on this machine. That field is compiled out of production DLLs.

`instanceId` is a plugin-generated UUID stored in OBS module config for the life of this install. Remote Deck uses it (not the machine name) to keep a unique OBS setup per instance. Re-auth with the same id updates that setup; a missing id creates a new one. Sign-out does not rotate the id.

The plugin stores `studioPluginToken`, `wssUrl`, `instanceId`, and studio metadata in OBS module config. It never logs the token.

## Control socket

```
WSS {wssUrl}   (default wss://remotedeck.gg/ws/plugin)
Authorization: Bearer <studioPluginToken>
```

TLS is required except for localhost. Missing/invalid bearer is a non-101 HTTP response.

JSON frames: [PROTOCOL.md](../PROTOCOL.md). Editor mute/volume is forwarded by Remote Deck onto this socket; producers never receive OBS host, port, or obs-websocket password.
