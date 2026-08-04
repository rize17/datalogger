# DataLogger — Water Meter Monitoring System

Self-hosted, free-tier water usage monitor. An ESP32 counts pulses from a
reed switch on the water meter and publishes readings over MQTT; a
phone-installable web app shows live and historical usage; a Cloudflare
Worker logs every reading permanently so history has no gaps.

> **Full architecture, conventions, and gotchas live in [`CLAUDE.md`](CLAUDE.md).**
> Read that first — it's the source of truth for how the pieces fit and the
> mistakes already solved.

## Architecture

```
Reed switch → ESP32 → MQTT (HiveMQ Cloud) ─┬─→ web app (live, instant)
                                            └─→ Cloudflare Worker (every 5 min) → D1
web app ← D1 (/history, /aggregate) for backfill + Day/Week/Month views
```

| Platform | Folder | Role |
|---|---|---|
| ESP32 firmware | `1-esp32-firmware/` | Counts pulses, publishes every 5 min |
| Web app (PWA) | `2-web-app/` | Live display + history, installed to home screen |
| Cloudflare logger | `3-cloudflare-logger/` | Always-on history backup in D1 |

## Quick start

1. **HiveMQ Cloud** — create a free Serverless cluster + credentials
   (`guides/CLOUD_SETUP.md`).
2. **Firmware** — set WiFi + MQTT values at the top of
   `1-esp32-firmware/water_meter_firmware.ino`, flash via Arduino IDE
   (needs PubSubClient + ArduinoJson). `TEST_MODE` must be `false` for real use.
3. **Web app** — push to `main`; the included Actions workflow publishes
   `2-web-app/` to GitHub Pages. Open the URL, add to home screen, then fill
   in the broker details + history URL in *Set up*.
4. **Cloudflare logger** — follow `3-cloudflare-logger/DASHBOARD_DEPLOY.md`
   (entirely in the browser, no terminal).

Wiring: reed switch GPIO 27→GND, button GPIO 14→GND (internal pull-ups, no
resistors).

## Security note

This repo is public, so **every credential in it is a placeholder** —
`YOUR_CLUSTER.s1.eu.hivemq.cloud`, `YOUR_MQTT_USER`, `YOUR_MQTT_PASSWORD`.
Fill in your own values locally before flashing the firmware or deploying the
Worker, and keep the filled-in versions out of git. The web app never stores
credentials in the repo at all — the broker host, username and password are
entered on its **Set up** screen and kept in the browser's local storage.

The Cloudflare Worker's `/history`, `/aggregate` and `/poll` endpoints require
an `API_KEY` shared secret (`X-API-Key` header) and fail closed if it isn't
configured — see `3-cloudflare-logger/DASHBOARD_DEPLOY.md`.

## Versioning

The web app shows a version badge in its header. Bump it on every functional
change — it's how you confirm a GitHub Pages deploy actually landed.
Current: **v1.3**.
