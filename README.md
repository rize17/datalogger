# DataLogger — water meter web app

The phone-installable app for a home water-meter monitor. It shows live usage
over MQTT and historical usage from a Cloudflare Worker, and installs to a
home screen as a PWA.

**Live at <https://rize17.github.io/datalogger/>**

This repository holds *only* the app. The meter firmware, the Cloudflare
Worker and the system documentation live in a separate private repository —
this one is public purely so GitHub Pages can serve it for free.

## What's here

```
2-web-app/
  index.html            the entire app — UI, chart and MQTT client in one file
  sw.js                 service worker, offline shell
  manifest.webmanifest  home-screen install metadata
  icon-192.png icon-512.png
```

## Setting it up

Open the app, tap **Set up**, and sign in:

| Field | What it is |
|---|---|
| Username / password | your account for the logger; decides which meters you see |
| Server URL | the Worker's base URL |
| Liters per pulse (K-factor) | check the meter's spec plate |

Signing in is all the app needs. Add these under **Live (optional)** too if
you want instant updates over MQTT, rather than waiting for the next Worker
poll:

| Field | What it is |
|---|---|
| Broker WebSocket URL | `wss://<cluster>:8884/mqtt` |
| Topic | `home/water/+/data` — the `+` covers every meter |
| Broker username / password | broker credentials |

Nothing is hardcoded and no credentials are stored in this repo. Everything
is kept in the browser's local storage; the sign-in is exchanged for a token
that lasts 30 days and renews itself.

## Deploying

Push to `main`. `.github/workflows/pages.yml` publishes `2-web-app/` to Pages
automatically, serving it at the site root.

Bump the version badge on every functional change — it's how you confirm a
deploy actually landed, since Pages caches hard and a hard-refresh is often
needed. Four places, all together: the `<title>`, the `.version` span, `CACHE`
in `sw.js`, and *Current* below.

Current: **v2.1**.
