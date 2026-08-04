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
- Save (this redeploys the Worker automatically)

## 7. Add the cron schedule

- Worker → **Settings** → **Triggers** → **Cron Triggers** → **Add Cron Trigger**
- Enter: `*/5 * * * *`
- Save

## 8. Test it

Your Worker has a URL shown at the top of its dashboard page, something like:

```
https://watermeter-logger.<your-subdomain>.workers.dev
```

Visit `<that URL>/poll` once in your browser — triggers an immediate check.
Then visit `<that URL>/history` — you should see one reading in the response.

After this, the cron trigger handles everything automatically every 5
minutes — nothing left to visit or maintain.

## 9. Point the app at it

Same **History API URL** field in the DataLogger app's Set up screen —
paste the Worker's base URL there.

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
