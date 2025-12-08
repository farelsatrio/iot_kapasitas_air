#include <WiFi.h>
#include <PubSubClient.h>
#include <NewPing.h>
#include <ArduinoJson.h>

// === KONFIGURASI WI-FI ===
const char* ssid = "JSON";
const char* password = "javascript";

// === THINGSBOARD ===
#define THINGSBOARD_SERVER "192.168.1.8"
#define THINGSBOARD_PORT 1883
#define DEVICE_TOKEN "f3llv0g8y1ig0e8ehz6x"

// === PIN ===
#define RELAY_PIN 19
#define TRIG_PIN 5
#define ECHO_PIN 18
#define MAX_DISTANCE 300  // cm

// === TANGKI (SESUAIKAN DENGAN FISIK ANDA!) ===
#define TANK_HEIGHT 80.0    // Tinggi total tangki dalam cm
#define SENSOR_OFFSET 2.0    // Jarak dari sensor ke permukaan atas tangki (cm)
#define MIN_THRESHOLD 20     // % air minimum → nyalakan pompa
#define MAX_THRESHOLD 80     // % air maksimum → matikan pompa

// === PROTEKSI ===
#define PUMP_COOLDOWN 3000          // Jeda minimal 3 detik (hanya mode otomatis)
#define TELEMETRY_INTERVAL 1000     // Kirim data tiap 1 detik
#define SENSOR_RETRY_MAX 3          // Maksimum error berturut-turut sebelum darurat

// === INISIALISASI ===
NewPing sonar(TRIG_PIN, ECHO_PIN, MAX_DISTANCE);
WiFiClient espClient;
PubSubClient client(espClient);

// === VARIABEL GLOBAL ===
float waterLevel = 0.0;
bool pumpState = false;
String mode = "automatic";  // Bisa: "automatic" atau "manual"
unsigned long lastTelemetry = 0;
unsigned long lastPumpAction = 0;
int sensorErrorCount = 0;
bool emergencyMode = false;

// === DEKLARASI FUNGSI ===
void on_message(char* topic, byte* payload, unsigned int length);
void reconnect();
void controlPump(bool state, bool isAutomatic);
float readWaterLevel();

// === CALLBACK MQTT ===
void on_message(char* topic, byte* payload, unsigned int length) {
  char jsonBuffer[length + 1];
  memcpy(jsonBuffer, payload, length);
  jsonBuffer[length] = '\0';

  // Cek apakah ini RPC request
  if (strstr(topic, "v1/devices/me/rpc/request/") != nullptr) {
    // Tangani RPC request
    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, jsonBuffer);
    if (error) {
      Serial.print("❌ RPC JSON error: ");
      Serial.println(error.c_str());
      return;
    }

    const char* method = doc["method"];
    if (strcmp(method, "setMode") == 0) {
      const char* newMode = doc["params"]["mode"];
      if (strcmp(newMode, "automatic") == 0 || strcmp(newMode, "manual") == 0) {
        mode = newMode;
        Serial.print("[RPC] Mode diubah ke: ");
        Serial.println(mode);
      }
    }
    else if (strcmp(method, "setPumpStatus") == 0) {
      bool cmd = doc["params"]["pumpStatus"].as<bool>();
      controlPump(cmd, false); // false = manual (tanpa cooldown)
      Serial.print("[RPC] Pompa ");
      Serial.println(cmd ? "NYALA" : "MATI");
    }

    // Kirim response balik ke ThingsBoard (opsional)
    int requestId = atoi(strrchr(topic, '/') + 1); // Ambil ID request
    String responseTopic = "v1/devices/me/rpc/response/" + String(requestId);
    String response = "{\"result\":\"success\"}";
    client.publish(responseTopic.c_str(), response.c_str());
  }
  // Jika bukan RPC, maka atribut (seperti sebelumnya)
  else if (strcmp(topic, "v1/devices/me/attributes") == 0) {
    // Handler untuk atribut lama (jika Anda masih ingin menerima dari sini juga)
    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, jsonBuffer);
    if (error) {
      Serial.print("❌ Attr JSON error: ");
      Serial.println(error.c_str());
      return;
    }

    if (doc.containsKey("mode")) {
      const char* newMode = doc["mode"];
      if (strcmp(newMode, "automatic") == 0 || strcmp(newMode, "manual") == 0) {
        mode = newMode;
        Serial.print("[ATTR] Mode diubah ke: ");
        Serial.println(mode);
      }
    }

    if (doc.containsKey("pumpStatus") && mode == "manual") {
      bool cmd = doc["pumpStatus"].as<bool>();
      controlPump(cmd, false);
      Serial.print("[ATTR] Pompa ");
      Serial.println(cmd ? "NYALA" : "MATI");
    }
  }
}

// === FUNGSI KONTROL POMPA ===
void controlPump(bool state, bool isAutomatic) {
  // Jangan nyalakan pompa jika dalam mode darurat
  if (emergencyMode && state) {
    Serial.println("⚠️ [DARURAT] Pompa diblokir karena sensor error!");
    return;
  }

  if (state != pumpState) {
    unsigned long now = millis();
    // Hanya terapkan cooldown di mode otomatis
    if (!isAutomatic || (now - lastPumpAction > PUMP_COOLDOWN)) {
      digitalWrite(RELAY_PIN, state ? LOW : HIGH); // Relay aktif LOW
      pumpState = state;
      lastPumpAction = now;
    }
  }
}

// === PEMBACAAN SENSOR ===
float readWaterLevel() {
  unsigned int rawDistance = sonar.ping_cm();

  // Deteksi error: 0 = gagal, atau jarak tidak masuk akal
  if (rawDistance == 0 || rawDistance > TANK_HEIGHT + 50) {
    sensorErrorCount++;
    if (sensorErrorCount >= SENSOR_RETRY_MAX) {
      emergencyMode = true;
      controlPump(false, true); // Matikan pompa demi keamanan
      return -1.0; // Tandai error kritis
    }
    return waterLevel; // Gunakan nilai terakhir sementara
  }

  // Bacaan valid → reset error counter
  sensorErrorCount = 0;
  emergencyMode = false;

  // Hitung level air dengan offset
  float distance = rawDistance - SENSOR_OFFSET;
  distance = constrain(distance, 0.0, TANK_HEIGHT);
  float level = 100.0 - (distance / TANK_HEIGHT * 100.0);
  return constrain(level, 0.0, 100.0);
}

// === REKONEKSI MQTT ===
void reconnect() {
  static unsigned long lastAttempt = 0;
  if (millis() - lastAttempt < 5000) return; // Coba tiap 5 detik
  lastAttempt = millis();

  Serial.print("Menghubungkan ke ThingsBoard...");
  if (client.connect("ESP32_Tank_Realtime", DEVICE_TOKEN, nullptr)) {
    Serial.println(" ✅");
    client.subscribe("v1/devices/me/attributes");
    client.subscribe("v1/devices/me/rpc/request/+");
  } else {
    Serial.print(" ❌ Gagal, rc=");
    Serial.println(client.state());
  }
}

// === SETUP ===
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== Sistem Monitoring Tangki Air - Versi Stabil ===");

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH); // Pompa mati (relay aktif LOW)

  // Sambung ke Wi-Fi
  WiFi.begin(ssid, password);
  Serial.print("Menghubungkan ke Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println("\n✅ Wi-Fi terhubung! IP: " + WiFi.localIP());

  client.setServer(THINGSBOARD_SERVER, THINGSBOARD_PORT);
  client.setCallback(on_message);
}

// === LOOP UTAMA ===
void loop() {
  // Reconnect MQTT jika perlu (non-blocking)
  if (!client.connected()) {
    reconnect();
    return;
  }
  client.loop();

  // Kirim telemetri sesuai interval
  if (millis() - lastTelemetry >= TELEMETRY_INTERVAL) {
    lastTelemetry = millis();

    // Baca level air
    float newLevel = readWaterLevel();
    if (newLevel >= 0) {
      waterLevel = newLevel;
    }

    // Log ke Serial
    Serial.print("[SENSOR] Level: ");
    if (waterLevel < 0) {
      Serial.print("ERROR!");
    } else {
      Serial.print(waterLevel, 1);
      Serial.print("%");
    }
    Serial.print(" | Mode: ");
    Serial.print(mode);
    Serial.print(" | Pompa: ");
    Serial.println(pumpState ? "NYALA" : "MATI");

    // Logika otomatis (hanya jika sensor OK)
    if (mode == "automatic" && waterLevel >= 0) {
      if (waterLevel < MIN_THRESHOLD) {
        controlPump(true, true);
      } else if (waterLevel > MAX_THRESHOLD) {
        controlPump(false, true);
      }
    }

    // Kirim telemetri ke ThingsBoard
    StaticJsonDocument<128> telemetry;
    telemetry["waterLevel"] = (waterLevel < 0) ? 0 : waterLevel;
    telemetry["pumpStatus"] = pumpState;
    telemetry["mode"] = mode.c_str();
    if (waterLevel < 0) {
      telemetry["alert"] = "sensor_error";
    }

    char buffer[128];
    serializeJson(telemetry, buffer);
    if (client.publish("v1/devices/me/telemetry", buffer)) {
      Serial.println("📡 Telemetri terkirim");
    } else {
      Serial.println("❌ Gagal kirim telemetri");
    }
  }
}
