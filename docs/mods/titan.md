# T.I.T.A.N mod (homelab status)

T.I.T.A.N is the homelab's status board and help desk for friends and family
([Wizard815/T.I.T.A.N](https://github.com/Wizard815/T.I.T.A.N)). The mod shows
its board on DASH, read-only:

- **Home card**: "All 12 up", "Plex is down" or "2 down: Plex, Minecraft".
  It's marked when something is down.
- **TITAN screen**: the counts (up, down, checking, notes for you), up to six
  down services with the reason ("down because Outside access is down"),
  then every service with a coloured dot. **More** pages through a long list;
  **Refresh** asks again now.

Fixing things stays in T.I.T.A.N's own page, where the guard, the allowlist
and your Yes apply. DASH can't restart anything.

## Set up

On T.I.T.A.N, set `TITAN_GLANCE_TOKEN` (`openssl rand -hex 32`) in its `.env`
and restart it. Then in DASH's `secrets.h`:

```cpp
#define DASH_TITAN_URL "http://titan.lan:8130"
#define DASH_TITAN_TOKEN "<the same token>"
```

For an `https://` address also set `DASH_TITAN_CA_PEM` (like
`DASH_FAITH_CA_PEM`). Leave the URL empty to leave the mod off.

DASH asks `GET /api/glance` every minute, and when you open the screen. The
token sees what a family member sees in T.I.T.A.N's room: no one's personal
services and no owner-only ones.

## What goes wrong

| You see | Fix |
| --- | --- |
| "token refused" | `DASH_TITAN_TOKEN` doesn't match `TITAN_GLANCE_TOKEN` |
| "off: set TITAN_GLANCE_TOKEN" | T.I.T.A.N has no glance token yet |
| "T.I.T.A.N has no /api/glance" | T.I.T.A.N is older than the glance; update it |
| "no reply" | T.I.T.A.N isn't running, or DASH isn't on the home network |
