# Going Online — Cloud MQTT Broker Setup

This replaces the "run Mosquitto at home" step entirely. Both the ESP32 and the web app connect straight to a free cloud broker over TLS, so the system works from anywhere with internet — no port forwarding, no fixed IP, no VPN.

## 1. Create a free HiveMQ Cloud cluster

1. Go to **console.hivemq.cloud** and sign up (free tier: 100 connections, 10 GB/month — plenty for one device).
2. Create a new **Free cluster**.
3. On the cluster's **Overview** page, copy the **Cluster URL** — looks like `xxxxxxxxxxxx.s1.eu.hivemq.cloud`.
4. Go to **Access Management → Manage Credentials**, add a set of credentials (username + password). Use these for *both* the ESP32 and the web app — one set is fine for a single device.

*(EMQX Cloud is a solid alternative with a similar free tier and setup flow, if you'd rather compare.)*

## 2. Flash the updated firmware

The `.ino` file above is already updated for this:

- `WiFiClientSecure` instead of plain `WiFiClient`
- Port **8883** (TLS) instead of 1883
- Username/password required (cloud brokers reject anonymous connections)

Edit the CONFIG block at the top:

```cpp
const char* WIFI_SSID     = "...";
const char* WIFI_PASSWORD = "...";
const char* MQTT_HOST     = "xxxxxxxxxxxx.s1.eu.hivemq.cloud";  // your cluster URL
const char* MQTT_USER     = "...";   // credentials from step 1
const char* MQTT_PASS     = "...";
```

Flash it. Open the Serial Monitor — you should see `Connecting to MQTT... connected`.

> **Note on `wifiClient.setInsecure()`:** this skips certificate validation but keeps the connection encrypted — a normal shortcut for hobby projects. If you want the broker's certificate properly verified, swap it for `wifiClient.setCACert(...)` with HiveMQ's published root CA (linked from their docs).

## 3. Point the web app at the cloud broker

In the app's **Set up** panel:

| Field | Value |
|---|---|
| Broker WebSocket URL | `wss://xxxxxxxxxxxx.s1.eu.hivemq.cloud:8884/mqtt` |
| Topic | `home/water/watermeter-01/data` |
| Username | same as firmware |
| Password | same as firmware |

(Note the `wss://` scheme and port **8884** — HiveMQ Cloud's WebSocket+TLS port, separate from the 8883 used by the firmware.)

## 4. Put the web app somewhere public too

Since the broker's now reachable from anywhere, host the app itself somewhere public rather than your home network — then it works over cellular, at work, anywhere:

- **Netlify Drop** (app.netlify.com/drop) — drag the unzipped folder in, get a URL in seconds, free.
- **GitHub Pages** — push the files to a repo, enable Pages in settings.
- **Vercel** — similar drag-and-drop deploy flow.

Any of these work fine for a handful of static files. Once it's live at a public URL, open it on your phone and **Add to Home Screen** as before — now it'll connect from anywhere, not just your WiFi.

## Recap of what changed vs. the local setup

| | Local (Mosquitto at home) | Cloud (HiveMQ) |
|---|---|---|
| Broker location | Your Pi/PC | Managed by HiveMQ |
| Port forwarding | N/A (LAN only) | Not needed |
| Encryption | Optional | Required (TLS) |
| Auth | Optional | Required |
| Reachable from | Home WiFi only | Anywhere |
| Cost | Free (your hardware) | Free tier, plenty for 1 device |
