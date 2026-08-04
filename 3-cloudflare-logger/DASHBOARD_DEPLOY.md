# Deploying entirely through the Cloudflare dashboard

No terminal, no CLI — every step below happens in the browser.

## 1. Create the D1 database

- Dashboard → **Workers & Pages** → **D1 SQL Database** (left sidebar) → **Create database**
- Name it `watermeter-db` → Create

## 2. Load the schema

- Open the database you just created → **Console** tab
- Paste this and run it:

```sql
CREATE TABLE IF NOT EXISTS readings (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  received_at INTEGER NOT NULL,
  device TEXT,
  seq INTEGER,
  interval_pulses INTEGER,
  total_pulses INTEGER
);

CREATE UNIQUE INDEX IF NOT EXISTS idx_device_seq ON readings(device, seq);
```

## 3. Create the Worker

- Dashboard → **Workers & Pages** → **Create** → **Create Worker**
- Name it `watermeter-logger` → Deploy (this creates a placeholder — you'll replace the code next)

## 4. Paste the code

- Click **Edit code** on your new Worker
- Select everything in the editor, delete it
- Paste in the entire contents of `index-dashboard.js`
- Click **Deploy** (or **Save and Deploy**, depending on the editor version)

## 5. Bind the D1 database to the Worker

- Go to your Worker → **Settings** → **Bindings** → **Add binding**
- Type: **D1 Database**
- Variable name: `DB` (must match exactly — the code reads `env.DB`)
- Database: select `watermeter-db`
- Save

## 6. Add your MQTT credentials

- Same **Settings** page → **Variables and Secrets** → **Add**
- Add `MQTT_USER` = your HiveMQ username → mark as **Secret** (encrypted)
- Add `MQTT_PASS` = your HiveMQ password → mark as **Secret**
- Add `API_KEY` = a long random string → mark as **Secret**
- Save (this redeploys the Worker automatically)

`API_KEY` is what protects `/history`, `/aggregate` and `/poll` — without it
anyone who learns the Worker's URL can read your data and trigger writes. The
Worker **fails closed**: if `API_KEY` isn't set those routes return 503 rather
than serving unauthenticated. Generate one with:

```bash
openssl rand -base64 32
```

## 7. Add the cron schedule

- Worker → **Settings** → **Triggers** → **Cron Triggers** → **Add Cron Trigger**
- Enter: `*/5 * * * *`
- Save

## 8. Test it

Your Worker has a URL shown at the top of its dashboard page, something like:

```
https://watermeter-logger.<your-subdomain>.workers.dev
```

These routes need the key, so append `?key=<your API_KEY>`:

Visit `<that URL>/poll?key=...` once in your browser — triggers an immediate
check. Then visit `<that URL>/history?key=...` — you should see one reading.

Without the key you get `401 Unauthorized`, which is the quickest way to
confirm the protection is actually on:

```bash
curl -i https://watermeter-logger.<your-subdomain>.workers.dev/history
```

The `?key=` form is for convenience in a browser address bar; query strings
end up in logs and history, so prefer the header for anything automated:

```bash
curl -H "X-API-Key: <your API_KEY>" https://watermeter-logger.<your-subdomain>.workers.dev/history
```

After this, the cron trigger handles everything automatically every 5
minutes — nothing left to visit or maintain. The cron path doesn't go through
the auth check, so scheduled logging keeps working regardless.

## 9. Point the app at it

In the DataLogger app's Set up screen, paste the Worker's base URL into
**History API URL**, and the same `API_KEY` value into **History API key**.
The app sends it as an `X-API-Key` header. If the key is missing or wrong the
Day/Week/Month views say so explicitly rather than failing silently.

Note that `ALLOWED_ORIGINS` at the top of the Worker lists which browser
origins may read the API. If you serve the app from somewhere other than
`https://rize17.github.io`, add that origin there too.

## Troubleshooting

**`/poll` then `/history` still shows `[]`**
- Dashboard → your Worker → **Logs** tab → click **Begin log stream**, then
  hit `/poll` again in another tab. Any error will show up there in real
  time — this is your equivalent of watching a terminal.
- Double check the `DB` binding name is exactly `DB` (case-sensitive) —
  a typo there is the most common cause of silent failures.

**CONNACK never arrives / times out every time**
- Double-check `MQTT_USER` / `MQTT_PASS` were saved correctly under
  Variables and Secrets — a stray space from copy-pasting is a common
  culprit.
