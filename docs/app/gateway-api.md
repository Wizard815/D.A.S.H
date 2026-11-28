# Gateway API (v1)

The gateway is your own server on the internet (for example
`https://dash.thefellashub.com`). The DASH app uses it when it is away from the
truck and home networks: when truckd or T.I.T.A.N don't answer, it asks the
gateway instead. Nothing here is a cloud service; you run it.

In the app this is `Gateway` in `android/core/src/main/kotlin/dash/core/Gateway.kt`.
It implements the same interfaces as the local clients (`AtlasOwner`,
`TitanBoard`) plus `ReportFeed`, so a gateway can be swapped in or out.

## Rules

- **HTTPS only.** The app refuses an `http://` gateway.
- Every request carries `Authorization: Bearer <gateway token>`. The gateway
  checks it and answers `401 {"error":"..."}` if it's wrong.
- The gateway keeps its **own** truckd owner token and T.I.T.A.N glance
  token (pair it with truckd like a phone). The phone's tokens never go to it.
  Unpairing the gateway in truckd cuts remote access.
- Errors are JSON: `{"error":"plain words"}`. The app shows the words.
- No redirects: the app doesn't follow them, so a token never goes elsewhere.

## Endpoints

### `GET /api/v1/reports?since=<unix s>`

Recent dash-mod v1 frames (see [../mods/README.md](../mods/README.md)), as
the gateway heard them (for example from MQTT `dash/v1/#`).

```json
{"now":1764300100,"frames":[
  {"m":"atlas","t":"hi","s":41,"from":"truckd","up":120,"ign":1,"mode":"driving","ts":1764300000},
  {"m":"atlas","t":"alert","s":88,"id":"a_17","k":"cooler","lvl":2,"msg":"Cooler at 6 °C for 20 min","ts":1764300050},
  {"m":"titan","t":"sum","s":"All 12 up"}
]}
```

- Oldest first, at most 200. `since` is optional; without it, the latest
  frame of each `m`/`t`, like MQTT's retained messages.
- Each frame should carry `ts` (unix seconds) when known; the app uses it as
  "heard at". Frames up to 1024 bytes are accepted (MQTT can carry over 240).

### `GET /api/v1/atlas/fob`

truckd's `GET /api/fob`, passed through as is (status code and body).

### `POST /api/v1/atlas/fob`

Body `{"c":"lock","nonce":"<16-40>","exp":<unix s>}`, the same as truckd's
`POST /api/cmd/fob`. `c` is `lock`, `unlock_driver`, `unlock_all` or `start`.
The gateway forwards it to truckd with its own owner token and passes back
truckd's status (`202` accepted) and body. The app sends the same nonce it
tried locally, so truckd never runs a command twice.

### `GET /api/v1/titan/glance`

T.I.T.A.N's `GET /api/glance`, passed through as is.

## Status codes

| Code | Meaning |
| --- | --- |
| 200 / 202 | done / accepted |
| 401 | wrong gateway token |
| 502 | truckd or T.I.T.A.N didn't answer the gateway (the truck may be off or out of signal) |
| 504 | it answered too slowly |

Remote start through the gateway is still checked by truckd: nonce, `exp`
at most 30 s ahead, and its safety checks.
