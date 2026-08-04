CREATE TABLE IF NOT EXISTS readings (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  received_at INTEGER NOT NULL,
  device TEXT,
  seq INTEGER,
  interval_pulses INTEGER,
  total_pulses INTEGER
);

CREATE UNIQUE INDEX IF NOT EXISTS idx_device_seq ON readings(device, seq);
