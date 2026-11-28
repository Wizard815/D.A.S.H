# Home Assistant mod

The mod shows a few Home Assistant entities on DASH, and alerts you when a
door opens or a lock is left open. It reads HA's REST API with a long-lived
token. It's read-only unless you turn toggles on.

- **Home card**: "Home Assistant", then "All quiet", "Front door open" or
  "2 alerts: Front door, Back door". It's marked when something is alerting.
- **HA screen**: a line with the same words, then one row per entity: its
  name, its state with the unit ("21.5 C", "open", "unavailable"), a coloured
  dot, and how long ago it changed. Alerts come first, with "Alert" on the
  row. **More** shows the rest when there are over five; **Refresh** asks
  again now.
- **Alerts**: when an alert entity turns on, open, unlocked or problem, DASH
  blinks amber and buzzes twice, again every 5 minutes, until you open the
  HA screen or the entity goes back.

Binary sensors are said the way HA says them: a door's `on` is "open", a leak
sensor's is "wet", motion is "motion" or "clear".

## Set up

In HA, open your profile, then **Security**, and make a **long-lived access
token**. Then in DASH's `secrets.h`:

```cpp
#define DASH_HA_URL "http://homeassistant.lan:8123"
#define DASH_HA_TOKEN "eyJhbGciOi..."
#define DASH_HA_ENTITIES "sensor.living_room_temperature,light.kitchen,switch.heater"
#define DASH_HA_ALERTS "binary_sensor.front_door,lock.back_door,binary_sensor.leak"
#define DASH_HA_ALLOW_TOGGLE 0
```

- No trailing slash on the URL. For `https://` also set `DASH_HA_CA_PEM`
  (like `DASH_FAITH_CA_PEM`); without it DASH refuses https.
- Ids are comma-separated, at most 8 across both lists. Alert entities are
  shown too, so don't list them twice. Use the exact entity id from HA
  (Settings, Entities).
- Leave the URL or the token empty to leave the mod off. The mod also has an
  On/Off in **Links**.

DASH asks `GET /api/states/<entity_id>` for each entity every 30 seconds while
Wi-Fi is up, one entity per pass, and when you open the screen.

## Toggles (optional)

With `DASH_HA_ALLOW_TOGGLE 1`, a `switch`, `light`, `input_boolean` or `fan`
row toggles with two taps: the first says "Tap again to turn off", the second
within 5 seconds calls `POST /api/services/<domain>/toggle`. Locks, covers and
alarms never toggle from DASH.

The token acts as the HA user who made it, so that user must be allowed to
call those services. HA can't make a token read-only: any token can change
things through the API. Keep toggles off for a read-only DASH, and keep
`secrets.h` private.

## What goes wrong

| You see | Fix |
| --- | --- |
| "token refused: check DASH_HA_TOKEN" | the token is wrong, or was deleted in HA |
| "set DASH_HA_ENTITIES in secrets.h" | no valid entity ids; ids are lowercase, like `light.kitchen` |
| a row says "not found" | that entity id doesn't exist in HA; check its spelling |
| a row says "unavailable" | HA can't reach that device right now |
| "https needs DASH_HA_CA_PEM" | the URL is https but no CA certificate is set |
| "no reply: is Home Assistant running?" | HA is down, the port is wrong, or DASH isn't on the home network |
| "odd reply: is this Home Assistant?" | the URL points at something else, like a login page in front of HA |
| "Not allowed: the token's user can't do that" | the token's user may not call that service |
| the screen stutters every 30 s on https | each request makes a new TLS connection; use fewer entities or plain http on the home network |
