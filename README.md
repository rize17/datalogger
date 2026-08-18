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
   `1-esp32-firmware/water_meter_firmware/water_meter_firmware.ino`, flash via Arduino IDE
   (needs PubSubClient + ArduinoJson). `TEST_MODE` must be `false` for real use.
3. **Web app** — push to `main`; the included Actions workflow publishes
   `2-web-app/` to GitHub Pages. Open the URL, add to home screen, then fill
   in the broker details + history URL in *Set up*.
4. **Cloudflare logger** — follow `3-cloudflare-logger/DASHBOARD_DEPLOY.md`
   (entirely in the browser, no terminal).

Wiring — internal pull-ups throughout, so no external resistors:

| Board | Reed switch | Button |
|---|---|---|
| WiFi ESP32 (`1-esp32-firmware/`) | GPIO 27 → GND | GPIO 14 → GND |
| LoRa node (`1b-lora-node-firmware/`) | GPIO 4 → GND | onboard PRG button (GPIO 0) |

The LoRa node differs because the Heltec S3's onboard LoRa, OLED and USB
occupy the pins the plain ESP32 uses.

## Security note

This repo is public, so **every credential in it is a placeholder** —
`YOUR_CLUSTER.s1.eu.hivemq.cloud`, `YOUR_MQTT_USER`, `YOUR_MQTT_PASSWORD`.
Fill in your own values locally before flashing the firmware or deploying the
Worker, and keep the filled-in versions out of git. The web app never stores
credentials in the repo at all — the broker host, username and password are
entered on its **Set up** screen and kept in the browser's local storage.

The Cloudflare Worker's `/devices`, `/history`, `/aggregate` and `/poll`
endpoints need a session token, obtained by signing in with a username and
password, and fail closed if no accounts are configured. There is no API key.
Accounts live in D1 and decide which meters each person sees; the token lasts
30 days, renews itself, and is revoked by deleting a row. See
`3-cloudflare-logger/DASHBOARD_DEPLOY.md`.

## Versioning

The web app shows a version badge in its header. Bump it on every functional
change — it's how you confirm a GitHub Pages deploy actually landed.
Current: **v2.0**.
