// ================================================================
// ESP32 Traffic Light → Supabase (REST API + polling)
//
// Required Libraries (install via Arduino Library Manager):
//   ArduinoJson  →  by Benoit Blanchon  (v7 recommended)
//   HTTPClient   →  built-in with ESP32 Arduino core
// ================================================================
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ----------------------------------------------------------------
// Pin Definitions
// ----------------------------------------------------------------
#define PIN_RED    25   // D2  → RED LED
#define PIN_YELLOW 26   // D4  → YELLOW LED
#define PIN_GREEN  27   // D16 → GREEN LED

// ----------------------------------------------------------------
// WiFi credentials
// ----------------------------------------------------------------
#define WIFI_SSID     "Salalima wifi 4G"
#define WIFI_PASSWORD "salalima424"

// ----------------------------------------------------------------
// Supabase credentials
// ----------------------------------------------------------------
#define SUPA_URL  "https://dbmquedtshlknzicpzva.supabase.co"
#define SUPA_KEY  "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6ImRibXF1ZWR0c2hsa256aWNwenZhIiwicm9sZSI6ImFub24iLCJpYXQiOjE3NzQ5MTk0OTMsImV4cCI6MjA5MDQ5NTQ5M30.Vg5QbtlTAZ31cN8nJouqJmXaGgBbzYN7_TJIIw5vr6Y"

// Table name in Supabase (singleton row with id=1)
#define SUPA_TABLE "traffic_light"
#define SUPA_ROW   "id=eq.1"

// How often to poll Supabase for commands (ms)
#define POLL_INTERVAL 2000

// ----------------------------------------------------------------
// Traffic light state
// ----------------------------------------------------------------
String        currentSignal = "red";
String        currentMode   = "auto";
int           durRed        = 30;
int           durYellow     = 5;
int           durGreen      = 25;
int           autoPhase     = 0;   // 0=red 1=yellow 2=green
unsigned long phaseStart    = 0;
unsigned long lastPoll      = 0;

String phaseNames[] = {"red", "yellow", "green"};

// ----------------------------------------------------------------
// Set LEDs
// ----------------------------------------------------------------
void applySignal(String signal) {
  digitalWrite(PIN_RED,    signal == "red"    ? HIGH : LOW);
  digitalWrite(PIN_YELLOW, signal == "yellow" ? HIGH : LOW);
  digitalWrite(PIN_GREEN,  signal == "green"  ? HIGH : LOW);
  Serial.println("Signal → " + signal);
}

// ----------------------------------------------------------------
// Auto cycle duration helper
// ----------------------------------------------------------------
int phaseDuration(int phase) {
  if (phase == 0) return durRed;
  if (phase == 1) return durYellow;
  return durGreen;
}

// ----------------------------------------------------------------
// Supabase PATCH — write fields to the singleton row
// ----------------------------------------------------------------
bool supaPatch(String jsonBody) {
  if (WiFi.status() != WL_CONNECTED) return false;

  HTTPClient http;
  String url = String(SUPA_URL) + "/rest/v1/" + SUPA_TABLE + "?" + SUPA_ROW;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("apikey", SUPA_KEY);
  http.addHeader("Authorization", "Bearer " + String(SUPA_KEY));
  http.addHeader("Prefer", "return=minimal");

  int code = http.PATCH(jsonBody);
  bool ok  = (code == 200 || code == 204);
  if (!ok) Serial.println("❌ PATCH error: " + String(code));
  http.end();
  return ok;
}

// ----------------------------------------------------------------
// Supabase GET — read the singleton row, parse JSON into doc
// ----------------------------------------------------------------
bool supaGet(JsonDocument &doc) {
  if (WiFi.status() != WL_CONNECTED) return false;

  HTTPClient http;
  String url = String(SUPA_URL) + "/rest/v1/" + SUPA_TABLE
               + "?" + SUPA_ROW + "&limit=1";
  http.begin(url);
  http.addHeader("apikey", SUPA_KEY);
  http.addHeader("Authorization", "Bearer " + String(SUPA_KEY));
  http.addHeader("Accept", "application/json");

  int code = http.GET();
  if (code != 200) {
    Serial.println("❌ GET error: " + String(code));
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  // Supabase returns an array even for a single row
  JsonDocument arr;
  DeserializationError err = deserializeJson(arr, payload);
  if (err || !arr.is<JsonArray>() || arr.as<JsonArray>().size() == 0) {
    Serial.println("❌ JSON parse error");
    return false;
  }

  doc.set(arr[0]);
  return true;
}

// ================================================================
void setup() {
  Serial.begin(115200);

  pinMode(PIN_RED,    OUTPUT);
  pinMode(PIN_YELLOW, OUTPUT);
  pinMode(PIN_GREEN,  OUTPUT);

  // Boot blink test
  digitalWrite(PIN_RED,    HIGH); delay(100); digitalWrite(PIN_RED,    LOW);
  digitalWrite(PIN_YELLOW, HIGH); delay(100); digitalWrite(PIN_YELLOW, LOW);
  digitalWrite(PIN_GREEN,  HIGH); delay(100); digitalWrite(PIN_GREEN,  LOW);

  // --- Connect to WiFi ---
  Serial.print("Connecting to WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected! IP: " + WiFi.localIP().toString());

  // --- Write initial state to Supabase ---
  String initBody = String("{")
    + "\"signal\":\"" + currentSignal + "\","
    + "\"mode\":\"" + currentMode + "\","
    + "\"dur_red\":" + durRed + ","
    + "\"dur_yellow\":" + durYellow + ","
    + "\"dur_green\":" + durGreen + ","
    + "\"esp32_online\":true,"
    + "\"ip\":\"" + WiFi.localIP().toString() + "\","
    + "\"uptime\":0"
    + "}";

  if (supaPatch(initBody)) {
    Serial.println("✅ Supabase init OK");
  } else {
    Serial.println("❌ Supabase init failed — check table/key");
  }

  applySignal(currentSignal);
  phaseStart = millis();
  Serial.println("Traffic Light System Ready!");
}

// ================================================================
void loop() {

  // ── Poll Supabase every POLL_INTERVAL ────────────────────────
  if (millis() - lastPoll >= POLL_INTERVAL) {
    lastPoll = millis();

    JsonDocument row;
    if (supaGet(row)) {
      Serial.println("✅ Supabase poll OK");

      // Read mode
      if (!row["mode"].isNull()) {
        currentMode = row["mode"].as<String>();
      }

      // Read durations
      if (!row["dur_red"].isNull())    durRed    = row["dur_red"].as<int>();
      if (!row["dur_yellow"].isNull()) durYellow = row["dur_yellow"].as<int>();
      if (!row["dur_green"].isNull())  durGreen  = row["dur_green"].as<int>();

      // Apply manual signal
      if (currentMode == "manual" && !row["signal"].isNull()) {
        String demanded = row["signal"].as<String>();
        if (demanded != currentSignal) {
          currentSignal = demanded;
          applySignal(currentSignal);
        }
      }
    } else {
      Serial.println("❌ Supabase poll failed");
    }
  }

  // ── Auto cycle ───────────────────────────────────────────────
  if (currentMode == "auto") {
    if ((millis() - phaseStart) / 1000 >= (unsigned long)phaseDuration(autoPhase)) {
      autoPhase     = (autoPhase + 1) % 3;
      phaseStart    = millis();
      currentSignal = phaseNames[autoPhase];
      applySignal(currentSignal);

      // Push new signal to Supabase
      supaPatch("{\"signal\":\"" + currentSignal + "\"}");
    }
  }

  // ── Heartbeat every 10s ──────────────────────────────────────
  static unsigned long lastHeartbeat = 0;
  if (millis() - lastHeartbeat > 10000) {
    lastHeartbeat = millis();

    String hb = String("{")
      + "\"uptime\":" + (int)(millis() / 1000) + ","
      + "\"esp32_online\":true,"
      + "\"ip\":\"" + WiFi.localIP().toString() + "\""
      + "}";
    supaPatch(hb);
  }

  // ── WiFi watchdog ────────────────────────────────────────────
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi lost — reconnecting...");
    WiFi.reconnect();
    delay(3000);
  }
}