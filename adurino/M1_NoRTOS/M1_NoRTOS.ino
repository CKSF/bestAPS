#ifdef ARDUINO
#include <WiFiNINA.h>
#include <ArduinoMqttClient.h>

// ==== WiFi/MQTT credentials (update to your values) ====
const char* WIFI_SSID  = "kibaaa2.4";
const char* WIFI_PASS  = "20230716";

const char* MQTT_HOST  = "mqtt-dev.precise.seas.upenn.edu";
const int   MQTT_PORT  = 1883;
const char* MQTT_USER  = "cis441-541_2025";
const char* MQTT_PASSW = "cukwy2-geNwit-puqced";

const char* TEAM       = "BestAPS"; // replace with your TEAM_NAME

// ---- Topic mapping (must match Codio side) ----
String PFX = String("cis441-541/") + TEAM;
String TOPIC_CGM_SUB   = PFX + "/cgm";            // subscribe
String TOPIC_INSULIN_P = PFX + "/insulin-pump";   // publish
String ATTR_REQ        = PFX + "/vp-attributes/request/OpenAPS";
String ATTR_RESP       = PFX + "/vp-attributes/response/OpenAPS";

WiFiClient wifiClient;
MqttClient mqttClient(wifiClient);

// shared state
volatile float current_BG = 0.0f;
volatile float current_time_min = 0.0f;
volatile bool  newBGData = false;

void onMqttMessage(int msgSize) {
  String topic = mqttClient.messageTopic();
  String payload;
  while (mqttClient.available()) payload += (char)mqttClient.read();

  if (topic == TOPIC_CGM_SUB) {
    int gIdx = payload.indexOf("\"Glucose\"");
    int tIdx = payload.indexOf("\"time\"");
    if (gIdx >= 0 && tIdx >= 0) {
      int gCol = payload.indexOf(':', gIdx);
      int tCol = payload.indexOf(':', tIdx);
      if (gCol > 0 && tCol > 0) {
        int gEnd = payload.indexOf(',', gCol + 1);
        String gStr = payload.substring(gCol + 1, gEnd > 0 ? gEnd : payload.length());
        gStr.trim();
        String tStr = payload.substring(tCol + 1); tStr.trim();
        float g = gStr.toFloat();
        float ts = tStr.toFloat();
        current_BG = g; current_time_min = ts; newBGData = true;
        Serial.print("[OpenAPS] CGM: BG="); Serial.print(g);
        Serial.print(" time="); Serial.println(ts);
      }
    }
  } else if (topic == ATTR_RESP) {
    Serial.println("[OpenAPS] vp-attributes response received (M1 ignore). ");
  } else {
    Serial.print("[OpenAPS] Ignored: "); Serial.println(topic);
  }
}

void publishBasal(float rate) {
  String msg = String("{\"insulin_rate\":") + String(rate, 3) + String("}");
  mqttClient.beginMessage(TOPIC_INSULIN_P, false, 1);
  mqttClient.print(msg);
  mqttClient.endMessage();
  Serial.print("[OpenAPS] Publish basal: "); Serial.println(msg);
}

// WiFi helpers
const char* wifiStatusName(int s) {
  switch (s) {
    case WL_IDLE_STATUS: return "IDLE";
    case WL_NO_SSID_AVAIL: return "NO_SSID";
    case WL_SCAN_COMPLETED: return "SCAN_DONE";
    case WL_CONNECTED: return "CONNECTED";
    case WL_CONNECT_FAILED: return "CONNECT_FAILED";
    case WL_CONNECTION_LOST: return "CONNECTION_LOST";
    case WL_DISCONNECTED: return "DISCONNECTED";
    default: return "UNKNOWN";
  }
}
void scanNetworksPrint() {
  Serial.println("\n[WiFi] Scanning networks...");
  int n = WiFi.scanNetworks();
  if (n <= 0) {
    Serial.println("[WiFi] No networks found");
    return;
  }
  for (int i = 0; i < n && i < 10; i++) {
    Serial.print(i + 1);
    Serial.print(") ");
    Serial.print(WiFi.SSID(i));
    Serial.print(" RSSI="); Serial.print(WiFi.RSSI(i));
    Serial.print(" enc="); Serial.println(WiFi.encryptionType(i));
  }
}

bool connectWiFiTry(const char* ssid, const char* pass, unsigned long timeout_ms) {
  WiFi.disconnect();
  delay(300);
  WiFi.begin(ssid, pass);
  unsigned long t0 = millis();
  unsigned long lastStatusLog = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print('.');
    if (millis() - lastStatusLog > 4000) {
      int st = WiFi.status();
      Serial.print(" status="); Serial.print(st);
      Serial.print(" ("); Serial.print(wifiStatusName(st)); Serial.println(")");
      lastStatusLog = millis();
    }
    if (millis() - t0 > timeout_ms) {
      Serial.println("\n[WiFi] Timeout");
      break;
    }
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("\nWiFi OK. IP="); Serial.println(WiFi.localIP());
    return true;
  }
  return false;
}

void setup() {
  Serial.begin(115200);
  unsigned long s = millis();
  while (!Serial && (millis() - s) < 8000) { /* 最多等待 8 秒以便监视器连接 */ }
  Serial.println("OpenAPS (M1, NoRTOS) start");

  Serial.print("Connecting WiFi "); Serial.println(WIFI_SSID);
  scanNetworksPrint();
  bool wifiOk = connectWiFiTry(WIFI_SSID, WIFI_PASS, 20000);
  if (!wifiOk) {
    Serial.println("[WiFi] Connect failed, retrying...");
    wifiOk = connectWiFiTry(WIFI_SSID, WIFI_PASS, 20000);
  }
  if (!wifiOk) {
    Serial.println("[WiFi] Still not connected. Check SSID/password and 2.4GHz.");
  }

  mqttClient.setId(String("OpenAPS-") + TEAM);
  mqttClient.setUsernamePassword(MQTT_USER, MQTT_PASSW);
  mqttClient.onMessage(onMqttMessage);

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[MQTT] Skipping MQTT setup (WiFi not connected)");
  } else {
    Serial.print("Connecting MQTT "); Serial.print(MQTT_HOST); Serial.print(":"); Serial.println(MQTT_PORT);
    if (!mqttClient.connect(MQTT_HOST, MQTT_PORT)) {
      Serial.print("MQTT connect failed, code=");
      Serial.println(mqttClient.connectError());
    } else {
      Serial.println("MQTT OK");
      mqttClient.subscribe(TOPIC_CGM_SUB, 1);
      mqttClient.subscribe(ATTR_RESP, 1);
      mqttClient.beginMessage(ATTR_REQ);
      mqttClient.print("{\\\"request\\\":\\\"PatientProfile\\\"}");
      mqttClient.endMessage();
      // (SelfTest removed)
    }
  }
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    static unsigned long lastWiFiTry = 0;
    if (millis() - lastWiFiTry >= 2000) {
      int st = WiFi.status();
      Serial.print("WiFi lost, reconnecting... status=");
      Serial.print(st);
      Serial.print(" ("); Serial.print(wifiStatusName(st)); Serial.println(")");
      WiFi.begin(WIFI_SSID, WIFI_PASS);
      lastWiFiTry = millis();
    }
    delay(100);
    return;
  }

  if (!mqttClient.connected()) {
    Serial.println("MQTT lost, reconnecting...");
    if (!mqttClient.connect(MQTT_HOST, MQTT_PORT)) {
      Serial.print("mqtt reconnect failed, code=");
      Serial.println(mqttClient.connectError());
      delay(500);
      return;
    }
    Serial.println("MQTT reconnected.");
    mqttClient.subscribe(TOPIC_CGM_SUB, 1);
    mqttClient.subscribe(ATTR_RESP, 1);
  }
  mqttClient.poll();

  if (newBGData) {
    float insulin_rate = (current_BG > 120.0f) ? 0.5f : 0.0f; // M1 placeholder
    publishBasal(insulin_rate);
    newBGData = false;
  }
  delay(100);
}

#else
// 非 Arduino 环境占位，避免静态分析报错
void setup() {}
void loop() {}
#endif