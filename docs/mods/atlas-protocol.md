# Atlas protocol (dash-mod v1)

How Atlas (truckd on the truck's Pi) and DASH talk. Agreed with the truck
homelab design. The general frame rules are in [README.md](README.md): one
JSON object, at most 240 bytes, `"m":"atlas"`, metric, missing means unknown.

## Paths

| Path | Notes |
| --- | --- |
| MQTT on the truck Wi-Fi | truckd publishes each frame retained to `dash/v1/atlas/<t>`; `dtx` goes to `dash/v1/atlas/dtx/<code>`. DASH publishes to `dash/v1/atlas/dash`. Mosquitto runs on the Pi. |
| ESP-NOW | through the Atlas bridge (ESP32-C3 on the Pi's UART) or any ESP peer. Channel 6: the truck AP is pinned there in hostapd. |
| UART Pi to bridge | one frame per line, 115200 8N1, `/dev/ttyAMA0` with `dtoverlay=miniuart-bt` |

## From Atlas

| `t` | Fields | When |
| --- | --- | --- |
| `hi` | `from` ("truckd"), `up` seconds, `ign` 0/1, `mode` driving / share / sync, `ts` | every 10 s |
| `bye` | `why` (ign_off, ...) | right before the Pi powers off. DASH shows "Asleep" instead of "no word". |
| `obd` | `ect` `iat` `oil` `atf` (deg C), `v` volts, `rpm`, `kph`, `fuel` %, `l100` and `l100a` (L/100 km now and trip average), `rng` km | every 2 s while driving |
| `dtc` | `mil` 0/1, `codes` up to 8, like `"P0420"`. An empty list means cleared. | on change and every 60 s |
| `dtx` | `c` code, `d` plain words up to 100 characters | optional, once per code. Without it DASH uses its own table for common codes, then the code's area. |
| `sync` | `st` waiting / syncing / synced / failed / off, `q` records waiting, `last` unix time of the last good sync, `via`, `err` | on change |
| `wx` | `c`, `rh` %, `hpa`, `txt`, `hi`, `lo` | every few minutes |
| `pwr` | `v`, `soc` % of the Pi's own battery | every minute |
| `gps` | `lat`, `lon`, `kph`, `hdg` | every 5 s while moving |
| `cmd` | `id` (the request's nonce), `what` (e.g. "start"), `ok` 0/1, `msg` plain words | after an owner command (remote start) sent over truckd's HTTP API, so every screen sees it happened |

Examples:

```json
{"m":"atlas","t":"hi","s":41,"from":"truckd","up":120,"ign":1,"mode":"driving","ts":1764214000}
{"m":"atlas","t":"obd","s":42,"ect":92,"iat":31,"oil":101,"atf":85,"v":14.1,"rpm":1800,"kph":96,"fuel":62,"l100":13.2,"l100a":12.8,"rng":402}
{"m":"atlas","t":"dtc","s":43,"mil":1,"codes":["P0420","P0171"]}
{"m":"atlas","t":"sync","s":44,"st":"waiting","q":37,"last":1764200000,"via":"home wifi","err":""}
{"m":"atlas","t":"bye","s":45,"why":"ign_off"}
```

## From DASH

| `t` | Meaning |
| --- | --- |
| `req` | send everything now. DASH sends it on connect, when its Atlas screen opens, and every minute over ESP-NOW while it hears nothing. |

## For the bridge itself (`"m":"bridge"`, UART only)

| `t` | Meaning |
| --- | --- |
| `ping` / `pong` | the bridge answers with its MAC |
| `need` | `sync` 1 or 0: whether data is waiting. The bridge also reads every `sync` frame it relays. |
| `hi` | the bridge says its MAC when it starts |

## Owner tools (not on these frames)

Pairing, managing the Truck Share, uploads and remote start go over truckd's
HTTP API with an owner key, from the DASH phone app's Atlas mod: see
[A.T.L.A.S docs/api.md](https://github.com/Wizard815/A.T.L.A.S/blob/main/docs/api.md).
Remote start carries a nonce and expires in 30 s or less; its result comes
back as a `cmd` frame.

## Rules

- DASH itself is read-only. Commands go over HTTP with the owner key, never
  as frames, so a spoofed ESP-NOW frame can't start the truck.
- Over 240 bytes: the bridge drops it; MQTT still delivers it.
