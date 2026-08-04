/*
 * ESP32 Water Meter — Reed Switch Pulse Counter (Cloud MQTT)
 * ------------------------------------------------------------
 * - Counts pulses from a reed switch (interrupt-driven, debounced)
 * - Persists cumulative count to NVS flash (survives reboot/power loss)
 * - Publishes JSON via MQTT (TLS) every 5 minutes:
 *     { "device":"...", "seq":N, "ts":<uptime_s>,
 *       "interval_pulses":N, "total_pulses":N }
 * - Physical button publishes an immediate event message (not tied to
 *   the 5-minute cycle):
 *     { "device":"...", "event":"button_pressed", "ts":N, "total_pulses":N }
 * - Connects to a cloud MQTT broker (HiveMQ Cloud free tier) over TLS on
 *   port 8883, so the device works from anywhere with WiFi — no home
 *   broker, port forwarding, or fixed IP required.
 * - TEST_MODE is off — this is the confirmed-working configuration with
 *   the real reed switch.
 *
 * Wiring:
 *   Reed switch: one leg -> GPIO 27, other leg -> GND
 *   Button:      one leg -> GPIO 14, other leg -> GND
 *   (internal pull-ups used for both; closing the circuit pulls the pin LOW)
 *
 * Libraries (Arduino IDE -> Library Manager):
 *   - PubSubClient by Nick O'Leary
 *   - ArduinoJson by Benoit Blanchon
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include <ArduinoJson.h>

// ======================= CONFIG =======================
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// Cloud MQTT broker (HiveMQ Cloud free cluster)
const char* MQTT_HOST     = "YOUR_CLUSTER.s1.eu.hivemq.cloud";
const uint16_t MQTT_PORT  = 8883;                              // TLS port
const char* MQTT_USER     = "YOUR_MQTT_USER";
const char* MQTT_PASS     = "YOUR_MQTT_PASSWORD";
const char* DEVICE_ID     = "watermeter-01";
const char* TOPIC_DATA    = "home/water/watermeter-01/data";
const char* TOPIC_STATUS  = "home/water/watermeter-01/status";

const uint8_t  PULSE_PIN          = 27;
const uint8_t  BUTTON_PIN         = 14;        // physical test/manual-trigger button
const uint32_t DEBOUNCE_MS        = 50;        // reed switches bounce; tune 20-100ms
const uint32_t BUTTON_DEBOUNCE_MS = 200;       // buttons bounce more than reed switches
const uint32_t PUBLISH_INTERVAL_MS = 5UL * 60UL * 1000UL;  // 5 minutes
const uint32_t NVS_SAVE_EVERY_PULSES = 10;     // flash-wear friendly checkpointing
const char* TOPIC_EVENT   = "home/water/watermeter-01/event";

// ---- TEST MODE ----
// While true, fake pulses are generated automatically every ~20s so you can
// confirm WiFi + MQTT + cloud broker all work end-to-end before the reed
// switch is wired up. Currently OFF — the real sensor is confirmed working.
const bool TEST_MODE = false;
const uint32_t TEST_PULSE_INTERVAL_MS = 20UL * 1000UL;
uint32_t lastTestPulseMs = 0;
// ======================================================

WiFiClientSecure wifiClient;
PubSubClient mqtt(wifiClient);
Preferences  prefs;

// --- Pulse counting (ISR-safe) ---
volatile uint32_t pulseCount = 0;        // pulses since boot (delta added to base)
volatile uint32_t lastPulseMs = 0;

// --- Button (ISR-safe) ---
volatile bool buttonPressedFlag = false;
volatile uint32_t lastButtonMs = 0;

uint32_t totalBase   = 0;                // total loaded from NVS at boot
uint32_t lastSavedTotal = 0;
uint32_t lastPublishedTotal = 0;
uint32_t seq = 0;
uint32_t lastPublishMs = 0;

portMUX_TYPE pulseMux = portMUX_INITIALIZER_UNLOCKED;

void IRAM_ATTR onPulse() {
  uint32_t now = millis();
  if (now - lastPulseMs >= DEBOUNCE_MS) {
    portENTER_CRITICAL_ISR(&pulseMux);
    pulseCount++;
    lastPulseMs = now;
    portEXIT_CRITICAL_ISR(&pulseMux);
  }
}

void IRAM_ATTR onButtonPress() {
  uint32_t now = millis();
  if (now - lastButtonMs >= BUTTON_DEBOUNCE_MS) {
    portENTER_CRITICAL_ISR(&pulseMux);
    buttonPressedFlag = true;
    lastButtonMs = now;
    portEXIT_CRITICAL_ISR(&pulseMux);
  }
}

uint32_t getTotalPulses() {
  uint32_t p;
  portENTER_CRITICAL(&pulseMux);
  p = pulseCount;
  portEXIT_CRITICAL(&pulseMux);
  return totalBase + p;
}

// ---- TEST MODE: simulate a few pulses periodically ----
// Adds 1-3 fake pulses every TEST_PULSE_INTERVAL_MS, going through the same
// counter the real reed switch uses, so publishReading() has real-looking
// data to send. Delete this whole block (and its call in loop()) once the
// physical sensor is verified working.
void simulatePulses() {
  uint32_t now = millis();
  if (now - lastTestPulseMs < TEST_PULSE_INTERVAL_MS) return;
  lastTestPulseMs = now;

  uint8_t fake = 1 + (esp_random() % 3);  // 1-3 pulses
  portENTER_CRITICAL(&pulseMux);
  pulseCount += fake;
  portEXIT_CRITICAL(&pulseMux);
  Serial.printf("[TEST MODE] simulated %u pulse(s)\n", fake);
}

// --- WiFi ---
void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  Serial.printf("Connecting to WiFi %s", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(WiFi.status() == WL_CONNECTED
                 ? "\nWiFi connected: " + WiFi.localIP().toString()
                 : "\nWiFi connect timed out (will retry)");
}

// --- MQTT ---
void connectMQTT() {
  if (mqtt.connected() || WiFi.status() != WL_CONNECTED) return;
  Serial.print("Connecting to MQTT... ");
  bool ok = mqtt.connect(DEVICE_ID, MQTT_USER, MQTT_PASS,
                          TOPIC_STATUS, 1, true, "offline");
  if (ok) {
    Serial.println("connected");
    mqtt.publish(TOPIC_STATUS, "online", true);
  } else {
    Serial.printf("failed, rc=%d\n", mqtt.state());
  }
}

// --- NVS persistence ---
void saveTotalToNVS(uint32_t total) {
  prefs.putUInt("total", total);
  lastSavedTotal = total;
}

void publishReading() {
  uint32_t total = getTotalPulses();
  uint32_t interval = total - lastPublishedTotal;
  uint32_t thisSeq = seq++;

  StaticJsonDocument<256> doc;
  doc["device"]          = DEVICE_ID;
  doc["seq"]             = thisSeq;
  doc["ts"]              = millis() / 1000;   // uptime seconds; app timestamps on receipt
  doc["interval_pulses"] = interval;
  doc["total_pulses"]    = total;

  char payload[256];
  size_t n = serializeJson(doc, payload);

  if (mqtt.publish(TOPIC_DATA, (const uint8_t*)payload, n, true)) {
    Serial.printf("Published: %s\n", payload);
    lastPublishedTotal = total;
  } else {
    Serial.println("Publish failed; will retry next interval");
  }

  saveTotalToNVS(total);  // checkpoint alongside every publish
}

void publishButtonEvent() {
  StaticJsonDocument<192> doc;
  doc["device"]       = DEVICE_ID;
  doc["event"]        = "button_pressed";
  doc["ts"]            = millis() / 1000;
  doc["total_pulses"] = getTotalPulses();  // context: reading at time of press

  char payload[192];
  size_t n = serializeJson(doc, payload);

  if (mqtt.publish(TOPIC_EVENT, (const uint8_t*)payload, n)) {
    Serial.printf("Button event published: %s\n", payload);
  } else {
    Serial.println("Button event publish failed");
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);

  // Restore lifetime total from flash
  prefs.begin("watermeter", false);
  totalBase = prefs.getUInt("total", 0);
  lastSavedTotal = totalBase;
  lastPublishedTotal = totalBase;
  Serial.printf("Restored total pulses from NVS: %u\n", totalBase);

  pinMode(PULSE_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PULSE_PIN), onPulse, FALLING);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), onButtonPress, FALLING);

  connectWiFi();

  // Cloud brokers use TLS with certs from public CAs. Skipping validation
  // (setInsecure) is the common quick-start approach for hobby projects —
  // traffic is still encrypted, just without pinning the broker's certificate.
  // For stricter security, replace this with wifiClient.setCACert(rootCA)
  // using HiveMQ Cloud's published root CA.
  wifiClient.setInsecure();

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setBufferSize(512);
  connectMQTT();

  lastPublishMs = millis();
}

void loop() {
  connectWiFi();
  connectMQTT();
  mqtt.loop();

  if (TEST_MODE) simulatePulses();  // TEST MODE — remove once real sensor confirmed

  // Handle button press — publishes immediately, independent of the 5-min cycle
  bool pressed;
  portENTER_CRITICAL(&pulseMux);
  pressed = buttonPressedFlag;
  buttonPressedFlag = false;
  portEXIT_CRITICAL(&pulseMux);
  if (pressed && mqtt.connected()) {
    publishButtonEvent();
  }

  uint32_t now = millis();

  // 5-minute publish tick
  if (now - lastPublishMs >= PUBLISH_INTERVAL_MS) {
    lastPublishMs = now;
    publishReading();
  }

  // Wear-friendly incremental save between publishes
  uint32_t total = getTotalPulses();
  if (total - lastSavedTotal >= NVS_SAVE_EVERY_PULSES) {
    saveTotalToNVS(total);
  }

  delay(50);
}
