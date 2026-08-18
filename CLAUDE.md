# DataLogger web app

The PWA half of a home water-meter monitor: live readings over MQTT, history
from a Cloudflare Worker. **This repo holds only the app.** The firmware, the
Worker and the full system documentation are in a separate private repo — if
you need to know how a reading is produced, or what the Worker guarantees,
that's where it's written down. Don't reconstruct it here.

## Layout

Everything is `2-web-app/index.html` — UI, chart and MQTT client in one file.
Alongside it: `sw.js`, `manifest.webmanifest`, and two icons. There is no
build step and no package manager.

## Conventions that matter

- **Don't add CDN scripts.** Chart.js was removed because ad blockers and
  network filters blocked the CDN and killed the boot script; the chart is
  hand-drawn on a `<canvas>` and must stay that way. One CDN dependency
  remains — `mqtt.js` from cdnjs — and it carries the same risk: blocked, the
  app reports "mqtt.js failed to load" and live updates stop, though history
  still works because that uses plain `fetch`. Vendoring it would remove the
  last external dependency. Until then, don't add a second.
- **Dedupe by `seq`, never by a time window.** Retained MQTT messages replay
  the last reading on every reconnect; a time-based check let stale data
  reappear as "new". A message whose `seq` matches the last stored reading is
  skipped.
- **No credentials in the repo.** The broker host, username and password come
  from the Set up screen into localStorage — `index.html` has nothing
  hardcoded, so don't "helpfully" add defaults for the host or user.
- **Always send `?device=` explicitly.** The Worker defaults to the account's
  *first* meter when it's omitted, which silently shows the wrong one.
- **The wildcard subscription carries other people's meters.** Everyone shares
  broker credentials, so `home/water/+/data` delivers every household's
  readings. Once the Worker has told the app which meters the account owns,
  anything else is discarded rather than stored or listed. That's a
  client-side tidy-up, not access control.
- **Bump the version on every functional change**, in all four places at once:
  the `<title>`, the `.version` span, `CACHE` in `sw.js`, and *Current* in
  `README.md`. It's the only way to confirm a Pages deploy landed.

## Talking to the Worker

The history login is a username and password, exchanged for a token that
lasts 30 days and renews itself. **There is no API key** — the header is
still *named* `X-API-Key` for compatibility, but what it carries is a session
token, and nothing else authenticates. Renaming it would mean changing the
Worker and every deployed app at the same moment, for no gain.

The token goes in a header rather than the query string so it stays out of
server logs and browser history. Changing the login invalidates any token
held for the old one.

Two responses mean "fix the login" rather than "fix the URL": **401** (wrong
or expired) and **503** (the Worker has no accounts configured). Asking for a
meter the account doesn't own returns an empty result, not a 403 — deliberate,
so nothing leaks about other people's meters, but it does mean a wrong device
id looks like "no data" rather than an error.

## Deploying

Push to `main`; `.github/workflows/pages.yml` publishes `2-web-app/` to Pages
at the site root. Hard-refresh to beat the cache.
