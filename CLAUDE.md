# DataLogger — Water Meter Monitoring System

A home water-meter monitor: an ESP32 counts reed-switch pulses and publishes
readings over MQTT; a phone-installable web app shows live + historical usage;
a Cloudflare Worker logs every reading permanently for gap-free history.

## Repository layout

- `1-esp32-firmware/water_meter_firmware/` — Arduino firmware for the ESP32
  (direct WiFi+MQTT node — used when the meter is in WiFi range)
- `1b-lora-node-firmware/` — alternate meter-side firmware for
  out-of-WiFi-range installs: reed switch + LoRa radio, no WiFi/MQTT stack.
  Pairs with `1c-lora-gateway/`. Hardware: Heltec WiFi LoRa 32 V3.2, 868MHz
  EU — bench-tested and working; not yet run with a real reed switch.
- `1c-lora-gateway/` — mains-powered bridge: receives LoRa from
  `1b-lora-node-firmware/`, republishes to the same MQTT topic/JSON shape as
  `1-esp32-firmware/`, so everything downstream is unaware which node type
  produced a reading. Same hardware as the node — bench-tested and working,
  see its README.
- `1d-lora-combined/` — **`1b` and `1c` merged into one sketch**, with the
  role (node or gateway) chosen in the setup portal rather than at compile
  time. The two ran on identical hardware and shared ~2/3 of their code,
  including 22 byte-identical constants — five of which (`LORA_FREQ_MHZ`,
  `LORA_BANDWIDTH`, `LORA_SF`, `LORA_CR`, `LORA_SYNC_WORD`) must agree
  between a node and its gateway, where a mismatch is *silent*: two boards
  that simply never hear each other. One constant now, so it can't happen.
  Compiles clean, but is not yet bench-tested; `1b`/`1c` stay until it is,
  then go. It needs the **Huge APP** partition scheme — see the deploy notes.
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
Reed switch → ESP32 (WiFi) ──────────────┐
                                          ├─→ MQTT (HiveMQ Cloud) ─┬─→ web app (live, instant)
Reed switch → LoRa node → LoRa gateway ───┘                       └─→ Cloudflare Worker (poll every 5 min) → D1
web app ← D1 (/history, /aggregate) for backfill and Day/Week/Month views
```

- Two front-end options publish into the same MQTT topic: the WiFi-direct
  ESP32 (`1-esp32-firmware/`), or a LoRa node + gateway pair
  (`1b-lora-node-firmware/` + `1c-lora-gateway/`, bench-tested and working)
  for installs where the meter is out of WiFi range. Everything
  downstream — web app, Worker, D1 — is the same regardless of which node
  type produced a reading.
- The publishing device sends a JSON reading every 5 minutes (retained) to
  `home/water/<device-id>/data`, and a button-press event to `.../event`.
- Readings carry `seq` (a counter persisted in NVS, so it never repeats across
  reboots — see the gotcha below), `interval_pulses`, and cumulative
  `total_pulses`. The app converts pulses→liters with a local K-factor. The
  LoRa node also sends `vbat` (volts) when a cell is fitted; treat payload
  fields as additive — consumers must ignore what they don't recognise rather
  than validating against a fixed key set.
- The web app holds a live MQTT-over-WebSocket connection for instant updates;
  it fetches from the Worker only to backfill gaps and drive history views.

## Multiple meters and accounts

One shared broker, Worker and D1 serve several meters across more than one
household. An "account" is a row in D1 owning a set of devices. **A username
and password are the only way in** — there is no API key, no `ACCOUNTS`
secret and no `API_KEY` fallback, and none of them should grow back. A
permanent bearer credential that had to live in a phone is exactly what this
design removed.

`POST /login` swaps the username and password for a session token, which the
app sends in the `X-API-Key` header (kept that name so deployed apps didn't
break). Anything in that header not starting with `wmt_` is refused without a
database lookup, so `resolveAccount()` is one branch long. Tokens last 30
days, renew a day early, are stored as a SHA-256 hash, and are revoked by
deleting a row.

Credentials are **issued, not chosen**: you write a username and password into
the `accounts` table and hand them over, the same way you would broker
credentials. Nobody signs up, and the app has no setup step beyond typing them
in. See the gotchas below for how a plaintext password becomes a hash.

- **`DEVICE_ID` is the primary key of the whole system.** Unique per physical
  meter across everyone sharing the broker. It names the topics
  (`home/water/<id>/data`), it's what D1's `UNIQUE(device, seq)` keys on, and
  it's what the Worker's account map grants access to. Firmware builds the
  topics from it by string-literal concatenation, so the id and its topics
  can't drift apart.
- **A LoRa gateway relays only the meters in its served-meters list** (the
  portal's *Meters served* field; it was the compile-time `SERVED_DEVICES`
  before the gateway moved to runtime config). LoRa
  is a broadcast medium, so where two pairs share a site each gateway hears
  both nodes; without the filter each would republish the other's readings
  under its own meter's name. It also must not ACK an unserved packet — the
  gateway that does serve it will, and two simultaneous ACKs collide, leaving
  the node reporting `GW MISSED` while everything actually worked. Ignored
  packets are counted separately from drops because they aren't a fault.
- **`DEVICE_LABEL` is the short form for the OLED header** ("AD1", "FACT1"),
  because the full `DEVICE_ID` won't fit beside the battery reading. Purely
  cosmetic — nothing keys on it — but set it per board or every screen on the
  bench looks identical.
- **Accounts live in D1**, in the `accounts` and `devices` tables, and nowhere
  else. Adding a meter is a row, not a Worker redeploy. There is no fallback
  of any kind behind them: an empty or missing `accounts` table means every
  protected endpoint answers 503 until it's seeded. That's deliberate — with
  no key to fall back on, "no accounts" can only mean "not set up yet", and
  serving anything would be the wrong answer.
- **An account row with no devices is valid and signs in fine — it just sees
  no meters.** Which looks exactly like a broken install, so seed an account
  and its devices together.
- **`devices.device_id` is immutable; `display_name` is not.** The id names
  the topics and keys `readings`, so renaming it would look like "this board
  moved to a different meter" and reset a pulse total that never moved. Rename
  the display name instead — that's what it's for.
- **Asking for a device your key doesn't own returns empty, not 403.** That's
  deliberate: a 403 would confirm the device exists and leak other accounts'
  meter names.
- **The Worker polls a wildcard** (`home/water/+/data`) and collects every
  retained reading in one session. MQTT has no end-of-burst signal, so
  collection stops after a quiet period with a hard timeout as backstop.
- **The app subscribes to the wildcard too**, keying readings, battery and
  K-factor by device. It always sends `?device=` explicitly — the Worker
  defaults to the account's first meter, which would silently show the wrong
  one.

## Key conventions & hard-won gotchas (don't regress these)

- **Each `.ino` sits in a subfolder of the same name — don't flatten it.**
  Arduino requires the sketch folder to match the `.ino` basename, and sketch
  names may not start with a digit, so `1c-lora-gateway/lora_gateway_firmware.ino`
  simply won't build: the IDE opens it read-only and the compile dies with
  "The system cannot find the path specified". The nested folder satisfies
  Arduino while keeping the `1-`/`1b-`/`1c-`/`2-`/`3-` prefixes that document
  the data flow.
- **`seq` is persisted in NVS and must never repeat for a device.** It used to
  reset to 0 on every boot while D1 enforces `UNIQUE(device, seq)` with
  `INSERT OR IGNORE` — so after a reboot every reading was silently dropped
  from history until seq climbed past the highest value already stored. The
  live view looked perfectly healthy throughout, which is what made it hard to
  notice. Both firmwares now restore seq from NVS and skip forward by
  `SEQ_BOOT_GAP` on boot, so an ill-timed reset can't reuse one. Gaps in seq
  are fine; reuse is not.
- **Dedupe by `seq`, never by a time window.** Retained MQTT messages replay
  the last reading on every reconnect; an earlier time-based check let stale
  data reappear as "new". The web app now skips a message if its `seq` matches
  the last stored reading.
- **Add no new CDN scripts to the web app.** Chart.js was removed because ad
  blockers / network filters blocked the CDN and crashed the boot script; the
  chart is hand-drawn on a `<canvas>` and must stay that way. One CDN script
  does remain — `mqtt.js` from cdnjs, which the live view depends on. It
  carries exactly the same risk: if it's blocked the app reports "mqtt.js
  failed to load" and live updates stop, though history views still work
  because those use plain `fetch`. Vendoring it locally would remove the last
  external dependency; until then, don't add a second one.
- **The Cloudflare Worker must stay dependency-free too.** The dashboard code
  editor can't run `npm install`, so the Worker hand-writes the MQTT 3.1.1
  packets over a native WebSocket. Do not reintroduce the `mqtt` package into
  `index-dashboard.js`.
- **New Worker routes that return data or write must go in `PROTECTED_PATHS`.**
  Auth is an allowlist at the top of `fetch()`, not a default — a route added
  without being listed is public. `/poll` is a GET that causes writes, which is
  why it's protected too. The check fails closed (503) when the `accounts`
  table is empty or missing; keep it that way rather than falling back to open.
- **`ALLOWED_ORIGINS` is not the access control.** CORS only constrains
  browsers. The session token is what actually protects the endpoints — don't
  "simplify" by dropping that check and relying on the origin allowlist.
- **`/login` is the one path deliberately outside `PROTECTED_PATHS`**, because
  it's the endpoint that issues credentials and requiring one would be
  circular. It writes, so it doesn't fit the "unlisted paths are harmless"
  reading of that list — it's guarded by a per-username lockout (10 failures /
  15 min → 429) and by answering an unknown username and a wrong password
  identically. Don't add a second exception, and don't "helpfully" make the
  401 more specific: the identical answer is what stops it being a free
  username oracle.
- **Two password columns, and the reason is worth understanding before
  touching either.** A stored password is PBKDF2 over a random salt, which the
  D1 console cannot compute — so `accounts.password` holds plain text when an
  admin sets it, and `handleLogin()` upgrades it to `password_hash` on that
  account's first successful login, NULLing the plain copy in the same
  statement. That's what lets credentials be issued from SQL without leaving a
  readable password behind, and without anyone having to remember to go back
  and clear it.
- **The plain column beats any stored hash, and that precedence *is* the reset
  mechanism.** Writing `accounts.password` means "this is their password now".
  If the hash won instead, an admin reset would appear to do nothing at all —
  the worst kind of failure, because the console shows the new value sitting
  there looking correct. `upgradeStoredPassword()` is best-effort by design:
  the login has already succeeded when it runs, so a failed write must never
  become a failed sign-in.
- **The `accounts.api_key` column is now `password`.** Existing databases need
  `ALTER TABLE accounts RENAME COLUMN api_key TO password;` run once by hand —
  re-running `schema.sql` will *not* do it, because `CREATE TABLE IF NOT
  EXISTS` leaves an existing table alone. The symptom of forgetting is
  `/login` returning 503.
- **Never write `password_hash` from SQL.** It's PBKDF2 over a random salt,
  stored as `pbkdf2-sha256$<iterations>$<hex>`, and the D1 console can't
  compute it — a hand-written value just never matches, with nothing to say
  why. Passwords are set through `POST /set-password` only.
- **The iteration count travels inside `password_hash`, on purpose.** Changing
  `PBKDF2_ITERATIONS` must never invalidate stored passwords, and this is what
  makes tuning it safe in either direction. It needs tuning because hashing is
  pure CPU and Cloudflare's free plan allows 10ms per request: 50,000 measures
  ~5.6ms, 100,000 ~9.8ms — which is why it isn't 100,000. A login failing with
  "Worker exceeded resource limits" means lower it, not abandon it.
- **The password hash is now load-bearing, which it wasn't at first.** While
  `api_key` sat in plain text in the same row granting identical access,
  cracking a hash was pointless — anyone reading `accounts` already had the
  key. Removing the key is what made PBKDF2 the thing actually standing
  between a copy of that table and someone's data. So don't reintroduce a
  *permanent* plaintext credential beside it — the issued `password` column is
  only tolerable because it's erased on first login, and that window is the
  one real cost of this design.
- **The app stores the history password in localStorage**, alongside the
  broker password that was already there. That's what lets an expired token
  renew itself without a prompt on a phone that's been asleep. Same posture as
  the rest of the app's credentials, flagged so it stays a conscious choice.
- **One silent retry on a 401, never a loop.** The app re-logins once and
  retries; a second 401 surfaces as `AUTH`. Retrying a wrong password is how
  an account walks itself into the lockout.
- **Workers `fetch()` can't load `wss://`.** Use `https://` for the MQTT URL in
  the Worker; the `Upgrade` header does the WebSocket switch. (This one silently
  wasted a debugging session.)
- **Floating GPIO 27 generates phantom pulses.** With the reed switch
  disconnected, the pin can pick up noise. Real fix is a 0.1µF cap from GPIO 27
  to GND; software debounce alone doesn't cover isolated noise glitches.
- **Test mode** injects fake pulses (~every 20s) for testing without the
  sensor. Must be off for real use, or fake + real pulses mix. The LoRa node
  banners it on its OLED, because fake pulses are otherwise indistinguishable
  from real ones all the way downstream. It's `TEST_MODE` in
  `1-esp32-firmware/` and a portal tick on the node — one that is always
  unticked when the portal opens, so any save turns it off. Note fake pulses
  accumulate into the same NVS total as real ones, which is what the portal's
  "reset pulse total" is for at commissioning.
- **The gateway ACKs before it publishes, not after.** A TLS publish can take
  longer than the node's `ACK_TIMEOUT_MS` listen window, so acknowledging
  after would make the node report `GW MISSED` whenever the broker was merely
  slow. The consequence is that an ACK means "radio hop worked", never
  "delivered" — don't let anyone "improve" this by moving it. `SEND_ACK`
  (gateway) and `EXPECT_ACK` (node) must be kept in step.
- **Retransmit under the ORIGINAL `seq`, never a fresh one.** The LoRa node
  resends an unconfirmed reading unchanged, relying on `UNIQUE(device, seq)` +
  `INSERT OR IGNORE` in D1 and the app's seq check to discard it if the first
  copy did land. Giving the retry a new `seq` would double-count the interval
  — a worse failure than the gap it fixes. Dedupe-by-seq is load-bearing for
  correctness here, not just tidiness.
- **`lastPublishedTotal` advances only on a confirmed send.** That's what makes
  unconfirmed pulses roll into the next reading instead of being lost, so
  don't "simplify" it back to advancing on transmit success. Requiring an ACK
  is gated on `ackSupported` (the gateway having replied at least once), so a
  gateway with `SEND_ACK` off doesn't send the node into permanent retry.
- **DIO1 fires on TxDone as well as RxDone — clear the flag after any
  transmit.** Both boards attach an interrupt to DIO1 for received packets,
  so every transmission also sets it. The gateway missed this after sending
  an ACK and booked a phantom `drop` for every relay (a giveaway 1:1
  `relayed`/`drop` ratio on its screen); the node handles it by clearing at
  the top of `awaitGatewayAck()`. Any new transmit path needs the same.
- **The LoRa node's button forces a reading, not just an event.** The `E`
  event topic has no subscriber — neither the app nor the Worker reads it — so
  a press that only sent an event would prove nothing from the far end. The
  forced reading carries `total_pulses`, which is what separates "reed switch
  dead" (reading arrives, total unchanged) from "chain broken" (nothing
  arrives). Don't reduce it back to event-only.
- **Nothing in the gateway's `loop()` may block on the network.** A blocking
  WiFi/MQTT retry stalls the loop, so the gateway misses the node's
  `ACK_TIMEOUT_MS` window and the node reports `GW MISSED` — blaming the radio
  for a WiFi fault. `serviceNetwork()` re-arms and returns; `loop()` handles a
  pending LoRa packet *before* touching the network. Boot is allowed to block,
  steady state is not.
- **The SX1262 needs SPI started on its own pins, or `radio.begin()` hangs
  forever.** `Module(NSS, DIO1, RST, BUSY)` uses the board's *default* SPI
  bus, which on a generic ESP32S3 Dev Module is not the Heltec's LoRa bus
  (SCK 9 / MISO 11 / MOSI 10). Get this wrong and the chip never answers,
  `BUSY` stays high, and RadioLib blocks with **no timeout and no error** —
  `setup()` simply stops dead with no clue why. Both sketches now call
  `loraSpi.begin(...)` and pass `loraSpi` into `Module`. Cost a bench session
  to find; don't remove it.
- **On the Heltec V3 boards, `Vext` (GPIO36, active LOW) gates OLED power.**
  Nothing appears on screen until it's pulled LOW — that's the first thing to
  check on a dark display, ahead of the I2C pins. Separately, `ADC_Ctrl`
  (GPIO37, also active LOW) gates the battery divider on GPIO1; don't confuse
  the two. OLED init is deliberately non-fatal in both sketches: a dark screen
  must never stop pulses being counted or relayed.
- **TLS is set to skip cert validation** (`setInsecure()` on the device, and the
  equivalent in the Worker). Encrypted but not pinned — acceptable for a
  personal project, flagged here so it's a conscious choice.
- **Broker credentials are held client-side, but never committed.** The app
  takes them from its Set up screen into localStorage — `index.html` has
  nothing hardcoded, so don't "helpfully" add defaults for the host or user.
  Fine for single-user personal use; would need a backend proxy to go
  multi-user/public.
- **Both Heltec sketches are portal-configured; `1-esp32-firmware/` is not
  yet.** `1c-lora-gateway/` and `1b-lora-node-firmware/` take everything
  per-install from a setup portal into NVS, so one binary serves every site
  and nothing is compiled in. `1-esp32-firmware/` still has the
  `FILL THIS IN` block with placeholder credentials — don't assume the
  pattern is repo-wide yet. Porting it is planned; follow the gateway's shape.
- **The node's counters must not simply follow its config.** Now that the
  meter id is settable from a phone, "move this board to another meter" no
  longer involves a reflash — so nothing forces the NVS erase that used to
  stop the new meter inheriting the old one's `total_pulses`. An `owner` key
  records which id the counters were accumulated under: reset `total` and
  `seq` if and only if the id changed, adopt (never wipe) when no owner is
  recorded, and leave both alone on any other edit. The separate "reset pulse
  total" tick zeroes the total but deliberately NOT `seq` — the total belongs
  to the meter, `seq` belongs to this device's history in D1, and rewinding it
  would collide with rows already stored.

## Versioning

The web app shows a version badge in its header (currently **v2.0**). Bump the
`<title>`, the `.version` span, `CACHE` in `sw.js`, and `Current:` in
`README.md` together on every functional change — it's how we
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
- Pins differ per board — the Heltec S3's onboard LoRa/OLED/USB take up the
  pins the plain ESP32 uses, so don't copy one board's wiring to the other:
  - WiFi ESP32 (`1-esp32-firmware/`): reed switch GPIO 27→GND, button GPIO 14→GND
  - LoRa node (`1b-lora-node-firmware/`): reed switch GPIO 4→GND; button is
    the onboard **PRG** button (GPIO 0), nothing to wire. GPIO 0 is a
    strapping pin — fine to press while running, don't hold it through a
    reset. Heltec's peripheral-safe list omits GPIO 0 and it reportedly
    doesn't read on some V3 boards; fall back to `BUTTON_PIN = 5` with a
    wired button if so.
  - Both use internal pull-ups, so no external resistors

## Deploying changes

- **Web app:** commit and push to `main`. The `.github/workflows/pages.yml`
  workflow publishes `2-web-app/` to GitHub Pages automatically — nothing is
  uploaded by hand any more. Bump the version badge; hard-refresh to beat the
  cache. Note the Pages URL serves `2-web-app/` at its root, so the app lives
  at `https://rize17.github.io/datalogger/`, not `/datalogger/2-web-app/`.
- **Worker:** paste `index-dashboard.js` into the Cloudflare dashboard editor →
  Deploy. Test with `/poll` (forces one read) then `/history`.
- **Firmware:** flash via Arduino IDE (needs PubSubClient + ArduinoJson libs).
  Both Heltec sketches also need WiFiManager (tzapu, 2.0.17+ for esp32 core
  3.x) for their setup portals, and are the two with nothing to edit before
  flashing — they're configured from a phone afterwards.
  The LoRa node/gateway pair also needs RadioLib + Adafruit SSD1306, and the
  `esp32` core with the **ESP32S3 Dev Module** board selected (plain **ESP32
  Dev Module** for `1-esp32-firmware/`). Each `.ino` lives in a subfolder of
  the same name — open that path, not the numbered parent, or Arduino refuses
  to build it.
- **`1d-lora-combined/` needs Partition Scheme "Huge APP (3MB No OTA/1MB
  SPIFFS)".** On the IDE's default 4MB layout that binary is 90% of a 1.2MB
  app partition — it does fit today, so this is a ceiling to raise before it
  bites, not a broken build. Its `sketch.yaml` sets the scheme for
  `arduino-cli`, which the **IDE does not read**: select it under Tools by
  hand. The Heltec V3 has 8MB of flash and the sketch does no OTA, so the
  second app slot Huge APP drops was never in use.
- **There is an Arduino toolchain on this dev machine after all.** The IDE
  bundles `arduino-cli` at `C:\Program Files\Arduino IDE\resources\app\lib\
  backend\resources\arduino-cli.exe`, and Arduino15 already holds esp32 core
  3.3.11 plus every library above — so firmware can be compile-checked here
  without downloading anything (`downloads.arduino.cc` is blocked by the
  proxy). A first build takes >10 minutes; later ones reuse the core cache.
  Compiling is not flashing: it proves the code is valid, never that a role
  works.

## Open / future ideas discussed

- LoRa support (`1b-lora-node-firmware/` + `1c-lora-gateway/`) solves
  **range**, not battery life — for meters too far from WiFi. Hardware is
  Heltec WiFi LoRa 32 V3.2, 868MHz EU, and the pair is bench-tested end to
  end: pulses → LoRa → gateway → MQTT → web app, with ACKs working, and the
  Worker authenticated so `/history` and the Day/Week/Month views work
  too. Remaining before a real install: a physical reed switch on the node
  (everything so far has run on `TEST_MODE`), and range at the meter's actual
  location.
- **Two boards must never share a `DEVICE_ID`.** The WiFi ESP32 and the LoRa
  pair both ship as `watermeter-01`, so out of the box only one may be powered
  at a time — running both gives two devices publishing under one identity with
  independent `seq` counters, which produces interleaved garbage that still
  looks like valid data, and D1's `UNIQUE(device, seq)` silently discards half
  of it. Now that the stack is multi-device, the fix is simply to give each
  board its own id rather than alternating between them.
- Battery operation is a separate, later concern: the current always-on WiFi
  design gets ~1 day on an 18650. Real battery life needs a deep-sleep
  redesign (wake-on-pulse), independent of the LoRa work above.
