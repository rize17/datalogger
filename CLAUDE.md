# DataLogger — Water Meter Monitoring System

A home water-meter monitor: an ESP32 counts reed-switch pulses and publishes
readings over MQTT; a phone-installable web app shows live + historical usage;
a Cloudflare Worker logs every reading permanently for gap-free history.

## Repository layout

- `1-esp32-firmware/water_meter_firmware.ino` — Arduino firmware for the ESP32
- `2-web-app/` — the DataLogger PWA (static, hosted on GitHub Pages)
  - `index.html` — the entire app (UI, chart, MQTT client) in one file
  - `manifest.webmanifest`, `sw.js`, `icon-192.png`, `icon-512.png`
- `3-cloudflare-logger/` — the always-on history logger
  - `index-dashboard.js` — the Worker (dependency-free, pasted into the CF dashboard)
  - `schema.sql` — D1 table definition
- `HANDOVER.md`, `guides/` — reference docs
- `.github/workflows/pages.yml` — publishes `2-web-app/` to GitHub Pages on push
- `SECRETS.local.md` — real credentials, gitignored, never committed

## Architecture & data flow

```
Reed switch → ESP32 → MQTT (HiveMQ Cloud) ─┬─→ web app (live, instant)
                                            └─→ Cloudflare Worker (poll every 5 min) → D1
web app ← D1 (/history, /aggregate) for backfill and Day/Week/Month views
```

- The ESP32 publishes a JSON reading every 5 minutes (retained) to
  `home/water/watermeter-01/data`, and a button-press event to `.../event`.
- Readings carry `seq` (a per-boot counter), `interval_pulses`, and cumulative
  `total_pulses`. The app converts pulses→liters with a local K-factor.
- The web app holds a live MQTT-over-WebSocket connection for instant updates;
  it fetches from the Worker only to backfill gaps and drive history views.

## Key conventions & hard-won gotchas (don't regress these)

- **Dedupe by `seq`, never by a time window.** Retained MQTT messages replay
  the last reading on every reconnect; an earlier time-based check let stale
  data reappear as "new". The web app now skips a message if its `seq` matches
  the last stored reading.
- **The web app must stay dependency-free (no CDN scripts).** Chart.js was
  removed because ad blockers / network filters blocked the CDN and crashed the
  boot script. The chart is hand-drawn on a `<canvas>`. Keep it that way.
- **The Cloudflare Worker must stay dependency-free too.** The dashboard code
  editor can't run `npm install`, so the Worker hand-writes the MQTT 3.1.1
  packets over a native WebSocket. Do not reintroduce the `mqtt` package into
  `index-dashboard.js`.
- **Workers `fetch()` can't load `wss://`.** Use `https://` for the MQTT URL in
  the Worker; the `Upgrade` header does the WebSocket switch. (This one silently
  wasted a debugging session.)
- **Floating GPIO 27 generates phantom pulses.** With the reed switch
  disconnected, the pin can pick up noise. Real fix is a 0.1µF cap from GPIO 27
  to GND; software debounce alone doesn't cover isolated noise glitches.
- **`TEST_MODE` in the firmware** injects fake pulses (~every 20s) for testing
  without the sensor. Must be `false` for real use, or fake + real pulses mix.
- **TLS is set to skip cert validation** (`setInsecure()` on the device, and the
  equivalent in the Worker). Encrypted but not pinned — acceptable for a
  personal project, flagged here so it's a conscious choice.
- **Broker credentials live in client-side code** (app + firmware). Fine for
  single-user personal use; would need a backend proxy to go multi-user/public.

## Versioning

The web app shows a version badge in its header (currently **v1.2**). Bump both
the `<title>` and the `.version` span on every functional change — it's how we
confirm a GitHub Pages deploy actually took effect (Pages caches aggressively;
a hard-refresh may be needed).

## Config reference

This repo is **public**, so every credential in the tracked files is a
placeholder (`YOUR_CLUSTER...`, `YOUR_MQTT_USER`, `YOUR_MQTT_PASSWORD`).
The real values live in `SECRETS.local.md`, which is gitignored and never
leaves the dev machine — read that file when you need an actual value, and
never paste one into a tracked file.

- Ports: `8883` device (TLS), `8884` browser (WSS, path `/mqtt`)
- K-factor: `0.5` L/pulse (calibrated against a measured volume)
- Pins: reed switch GPIO 27→GND, button GPIO 14→GND (internal pull-ups)

## Deploying changes

- **Web app:** commit and push to `main`. The `.github/workflows/pages.yml`
  workflow publishes `2-web-app/` to GitHub Pages automatically — nothing is
  uploaded by hand any more. Bump the version badge; hard-refresh to beat the
  cache. Note the Pages URL serves `2-web-app/` at its root, so the app lives
  at `https://rize17.github.io/datalogger/`, not `/datalogger/2-web-app/`.
- **Worker:** paste `index-dashboard.js` into the Cloudflare dashboard editor →
  Deploy. Test with `/poll` (forces one read) then `/history`.
- **Firmware:** flash via Arduino IDE (needs PubSubClient + ArduinoJson libs).

## Open / future ideas discussed

- Battery operation is impractical on the current always-on WiFi design (~1 day
  on an 18650). Real battery life needs a deep-sleep redesign (wake-on-pulse) or
  a LoRa architecture (years, but a bigger rebuild + a mains-powered gateway).
