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
  medivac/              a second, unrelated tool — see below
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

## Medivac ETA

A separate calculator that shares the site, at
<https://rize17.github.io/datalogger/medivac/>. It answers the question you
otherwise have to answer with a pencil and Google Earth: a ship is out there,
when does she reach port, and when does she come inside the range you can fly
to?

Type the ship's position the way MarineTraffic prints it — `33.6833° S,
10.1050° E`, or degrees and minutes, or degrees minutes seconds — add her
speed, and it gives you the distance and ETA to the port and to each range
ring, plus the position she'll be at when she crosses each one. There's a tab
for the destinations you use often, seeded with the South African ports. The
rings default to 100 and 80 nm — the outer limit and the one you'd normally
work to. Put in the cruise speed of whatever you're flying (110 kt unless you
change it) and it also works back to the latest time you can lift and still
meet her at each ring. It doesn't allow for getting the machine ready; that
comes off your own clock, on top.

Distances are great-circle in nautical miles. Everything assumes she holds the
speed and course you gave it, so redo it when a fresh position comes in. It
keeps nothing on a server — the destinations and the last job you typed live
in the browser.

It has its own version, its own service worker and its own icon, and shares
nothing with the water meter app but the domain. Bump *its* version in four
places together, the same way: the `<title>`, the `.version` span, `CACHE` in
`medivac/sw.js`, and *Current* below.

Current: **medivac v1.1**.

## Deploying

Push to `main`. `.github/workflows/pages.yml` publishes `2-web-app/` to Pages
automatically, serving it at the site root.

Bump the version badge on every functional change — it's how you confirm a
deploy actually landed, since Pages caches hard and a hard-refresh is often
needed. Four places, all together: the `<title>`, the `.version` span, `CACHE`
in `sw.js`, and *Current* below.

Current: **v2.3** (water meter app).
