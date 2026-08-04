/*
 * Dashboard-only version — no npm dependencies, no build step.
 * Paste this entire file directly into the Cloudflare dashboard's Worker
 * code editor. It implements just enough of the MQTT 3.1.1 protocol by
 * hand (CONNECT, SUBSCRIBE, PUBLISH, DISCONNECT) over a native WebSocket
 * to grab one retained reading every 5 minutes and store it in D1.
 */

const MQTT_URL = "https://YOUR_CLUSTER.s1.eu.hivemq.cloud:8884/mqtt";
const MQTT_TOPIC = "home/water/watermeter-01/data";
const FETCH_TIMEOUT_MS = 10_000;

const CORS_HEADERS = {
  "Access-Control-Allow-Origin": "*",
  "Access-Control-Allow-Methods": "GET, OPTIONS",
};

/* ---------- MQTT packet encoding helpers ---------- */

function encodeUtf8String(str) {
  const strBytes = new TextEncoder().encode(str);
  const out = new Uint8Array(2 + strBytes.length);
  out[0] = (strBytes.length >> 8) & 0xff;
  out[1] = strBytes.length & 0xff;
  out.set(strBytes, 2);
  return out;
}

function encodeRemainingLength(length) {
  const bytes = [];
  do {
    let byte = length % 128;
    length = Math.floor(length / 128);
    if (length > 0) byte |= 0x80;
    bytes.push(byte);
  } while (length > 0);
  return new Uint8Array(bytes);
}

function decodeRemainingLength(bytes, startIndex) {
  let multiplier = 1;
  let value = 0;
  let index = startIndex;
  let encodedByte;
  do {
    encodedByte = bytes[index++];
    value += (encodedByte & 127) * multiplier;
    multiplier *= 128;
  } while ((encodedByte & 128) !== 0);
  return { value, nextIndex: index };
}

function concatBytes(arrays) {
  const total = arrays.reduce((sum, a) => sum + a.length, 0);
  const out = new Uint8Array(total);
  let offset = 0;
  for (const a of arrays) { out.set(a, offset); offset += a.length; }
  return out;
}

function buildConnectPacket(clientId, username, password, keepAliveSec) {
  const protocolName = encodeUtf8String("MQTT");
  const protocolLevel = new Uint8Array([4]); // MQTT 3.1.1

  let connectFlags = 0x02; // Clean Session
  if (username) connectFlags |= 0x80;
  if (password) connectFlags |= 0x40;

  const keepAlive = new Uint8Array([(keepAliveSec >> 8) & 0xff, keepAliveSec & 0xff]);
  const variableHeader = concatBytes([protocolName, protocolLevel, new Uint8Array([connectFlags]), keepAlive]);

  const payloadParts = [encodeUtf8String(clientId)];
  if (username) payloadParts.push(encodeUtf8String(username));
  if (password) payloadParts.push(encodeUtf8String(password));
  const payload = concatBytes(payloadParts);

  const remaining = concatBytes([variableHeader, payload]);
  const remainingLengthBytes = encodeRemainingLength(remaining.length);
  const fixedHeader = new Uint8Array([0x10]); // CONNECT

  return concatBytes([fixedHeader, remainingLengthBytes, remaining]);
}

function buildSubscribePacket(packetId, topic, qos) {
  const variableHeader = new Uint8Array([(packetId >> 8) & 0xff, packetId & 0xff]);
  const payload = concatBytes([encodeUtf8String(topic), new Uint8Array([qos])]);
  const remaining = concatBytes([variableHeader, payload]);
  const remainingLengthBytes = encodeRemainingLength(remaining.length);
  const fixedHeader = new Uint8Array([0x82]); // SUBSCRIBE, flags 0010

  return concatBytes([fixedHeader, remainingLengthBytes, remaining]);
}

const DISCONNECT_PACKET = new Uint8Array([0xe0, 0x00]);

function parsePacket(bytes) {
  const packetType = bytes[0] >> 4;
  const { value: remainingLength, nextIndex } = decodeRemainingLength(bytes, 1);
  const body = bytes.slice(nextIndex, nextIndex + remainingLength);
  return { packetType, body };
}

function parsePublish(byte0, body) {
  const qos = (byte0 >> 1) & 0x03;
  const topicLen = (body[0] << 8) | body[1];
  let offset = 2 + topicLen;
  if (qos > 0) offset += 2; // skip packet identifier, present only for QoS > 0
  return { payload: body.slice(offset) };
}

/* ---------- the actual connect-subscribe-grab-disconnect flow ---------- */

function fetchLatestReading(env) {
  return new Promise((resolve) => {
    let settled = false;
    let ws = null;

    const finish = (result) => {
      if (settled) return;
      settled = true;
      clearTimeout(timer);
      try {
        if (ws) { ws.send(DISCONNECT_PACKET); ws.close(); }
      } catch (_) {}
      resolve(result);
    };

    const timer = setTimeout(() => finish(null), FETCH_TIMEOUT_MS);

    (async () => {
      try {
        const resp = await fetch(MQTT_URL, {
          headers: { Upgrade: "websocket", "Sec-WebSocket-Protocol": "mqtt" },
        });
        ws = resp.webSocket;
        if (!ws) { finish(null); return; }

        ws.accept();
        ws.binaryType = "arraybuffer";

        ws.addEventListener("message", (event) => {
          if (!(event.data instanceof ArrayBuffer)) return;
          const bytes = new Uint8Array(event.data);
          const { packetType, body } = parsePacket(bytes);

          if (packetType === 2) {
            // CONNACK — body[1] is the return code, 0 = accepted
            if (body[1] !== 0) { finish(null); return; }
            ws.send(buildSubscribePacket(1, MQTT_TOPIC, 0));
          } else if (packetType === 3) {
            // PUBLISH — this carries the retained reading
            const { payload } = parsePublish(bytes[0], body);
            try {
              finish(JSON.parse(new TextDecoder().decode(payload)));
            } catch {
              finish(null);
            }
          }
        });

        ws.addEventListener("close", () => finish(null));
        ws.addEventListener("error", () => finish(null));

        const clientId = "cf-cron-" + Math.random().toString(16).slice(2, 8);
        ws.send(buildConnectPacket(clientId, env.MQTT_USER, env.MQTT_PASS, 30));
      } catch (_) {
        finish(null);
      }
    })();
  });
}

async function logReading(env) {
  const data = await fetchLatestReading(env);
  if (!data || data.seq == null) return;

  await env.DB.prepare(
    `INSERT OR IGNORE INTO readings (received_at, device, seq, interval_pulses, total_pulses)
     VALUES (?, ?, ?, ?, ?)`
  ).bind(
    Date.now(),
    data.device ?? null,
    data.seq,
    data.interval_pulses ?? 0,
    data.total_pulses ?? 0
  ).run();
}

/* ---------- Worker entry points ---------- */

export default {
  async fetch(request, env) {
    if (request.method === "OPTIONS") {
      return new Response(null, { headers: CORS_HEADERS });
    }

    const url = new URL(request.url);

    if (url.pathname === "/history") {
      const limit = Math.min(parseInt(url.searchParams.get("limit") || "288", 10), 1000);
      const { results } = await env.DB.prepare(
        `SELECT received_at, device, seq, interval_pulses, total_pulses
         FROM readings ORDER BY id DESC LIMIT ?`
      ).bind(limit).all();
      return new Response(JSON.stringify(results.reverse()), {
        headers: { "Content-Type": "application/json", ...CORS_HEADERS },
      });
    }

    // Aggregated history for the Day / Week / Month views.
    //   ?bucket=hour|day  — size of each bar
    //   ?since=<ms>       — only readings at/after this epoch-ms timestamp
    // Sums interval_pulses per bucket entirely in SQL, so the phone
    // downloads a handful of rows instead of thousands.
    if (url.pathname === "/aggregate") {
      const bucket = url.searchParams.get("bucket") === "hour" ? "hour" : "day";
      const since = parseInt(url.searchParams.get("since") || "0", 10);

      // received_at is epoch-ms; SQLite datetime works in seconds.
      const groupExpr = bucket === "hour"
        ? "strftime('%Y-%m-%dT%H:00', received_at/1000, 'unixepoch')"
        : "strftime('%Y-%m-%d', received_at/1000, 'unixepoch')";

      const { results } = await env.DB.prepare(
        `SELECT ${groupExpr} AS bucket,
                SUM(interval_pulses) AS pulses,
                COUNT(*) AS samples
         FROM readings
         WHERE received_at >= ?
         GROUP BY bucket
         ORDER BY bucket ASC`
      ).bind(since).all();

      return new Response(JSON.stringify(results), {
        headers: { "Content-Type": "application/json", ...CORS_HEADERS },
      });
    }

    if (url.pathname === "/poll") {
      await logReading(env);
      return new Response("ok", { headers: CORS_HEADERS });
    }

    return new Response("watermeter-logger: use /history or /poll", { headers: CORS_HEADERS });
  },

  async scheduled(_event, env, ctx) {
    ctx.waitUntil(logReading(env));
  },
};
