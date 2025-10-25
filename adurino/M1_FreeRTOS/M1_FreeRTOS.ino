// OpenAPS simplified controller for Nano 33 IoT with FreeRTOS (SAMD21)
#include <WiFiNINA.h>
#include <ArduinoMqttClient.h>
#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
extern "C" size_t xPortGetFreeHeapSize(void);

// ---- Fill your WiFi & MQTT credentials ----
const char* WIFI_SSID = "kibaaa2.4";
const char* WIFI_PASS = "20230716";

const char* MQTT_HOST = "mqtt-dev.precise.seas.upenn.edu";
const int   MQTT_PORT = 1883;
const char* MQTT_USER = "cis441-541_2025";
const char* MQTT_PASSWD = "cukwy2-geNwit-puqced";

const char* TEAM = "BestAPS"; // replace with your TEAM_NAME

// ---- Topic mapping (must match Codio side & M2 Notes) ----
String PFX = String("cis441-541/") + TEAM;
String TOPIC_CGM_SUB   = PFX + "/cgm-openaps";        // subscribe (per M2 Notes)
String TOPIC_INSULIN_P = PFX + "/insulin-pump-openaps"; // publish (per M2 Notes)
String ATTR_REQ        = PFX + "/vp-attributes/request/OpenAPS";
String ATTR_RESP       = PFX + "/vp-attributes/response/OpenAPS";
String SELFTEST = PFX + "/selftest";
String TOPIC_CGM_RAW   = PFX + "/cgm";                 // also handle raw VP topic

WiFiClient   wifiClient;
MqttClient   mqttClient(wifiClient);

// shared state
volatile float current_BG = 0.0f;
volatile float current_time_min = 0.0f;
volatile bool  newBGData = false;
SemaphoreHandle_t stateMutex;
TaskHandle_t hTaskHeartbeat = NULL;
TaskHandle_t hTaskMQTT = NULL;
TaskHandle_t hTaskOpenAPS = NULL;

void onMqttMessage(int msgSize) {
  String topic = mqttClient.messageTopic();
  String payload;
  while (mqttClient.available()) payload += (char)mqttClient.read();

  Serial.print("[MQTT] msg arrived topic="); Serial.print(topic);
  Serial.print(" len="); Serial.println(msgSize);
  
  if (topic == TOPIC_CGM_SUB || topic == TOPIC_CGM_RAW) {
    int gIdx = payload.indexOf("\"Glucose\"");
    int tIdx = payload.indexOf("\"time\"");
    if (gIdx >= 0 && tIdx >= 0) {
      int gCol = payload.indexOf(':', gIdx);
      int tCol = payload.indexOf(':', tIdx);
      if (gCol > 0 && tCol > 0) {
        String gStr = payload.substring(gCol + 1, payload.indexOf(',', gCol + 1) > 0 ? payload.indexOf(',', gCol + 1) : payload.length());
        gStr.trim();
        String tStr = payload.substring(tCol + 1); tStr.trim();
        float g = gStr.toFloat();
        float ts = tStr.toFloat();
        if (stateMutex && xSemaphoreTake(stateMutex, (TickType_t)10) == pdTRUE) {
          current_BG = g; current_time_min = ts; newBGData = true;
          xSemaphoreGive(stateMutex);
        }
        Serial.print("[OpenAPS] CGM: BG="); Serial.print(g);
        Serial.print(" time="); Serial.println(ts);
      }
    }
  } else if (topic == ATTR_RESP) {
    Serial.println("[OpenAPS] vp-attributes response received (M1 ignore). ");
  } else if (topic == SELFTEST) {
    Serial.print("[MQTT] Selftest echoed: "); Serial.println(payload);
  } else {
    Serial.print("[OpenAPS] Ignored: "); Serial.println(topic);
  }
  Serial.flush();
}

void publishBasal(float rate) {
  String msg = String("{\"insulin_rate\":") + String(rate, 3) + String("}");
  mqttClient.beginMessage(TOPIC_INSULIN_P, false, 1);
  mqttClient.print(msg);
  mqttClient.endMessage();
  Serial.print("[OpenAPS] Publish basal: "); Serial.println(msg);
}

// WiFi helpers (diagnostics)
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
  bool targetFound = false; int targetRssi = 0; int targetEnc = 0;
  for (int i = 0; i < n; i++) {
    if (i < 10) {
      Serial.print(i + 1);
      Serial.print(") ");
      Serial.print(WiFi.SSID(i));
      Serial.print(" RSSI="); Serial.print(WiFi.RSSI(i));
      Serial.print(" enc="); Serial.println(WiFi.encryptionType(i));
    }
    if (String(WiFi.SSID(i)) == String(WIFI_SSID)) {
      targetFound = true; targetRssi = WiFi.RSSI(i); targetEnc = WiFi.encryptionType(i);
    }
  }
  if (targetFound) {
    Serial.print("[WiFi] Target SSID found: "); Serial.print(WIFI_SSID);
    Serial.print(" RSSI="); Serial.print(targetRssi);
    Serial.print(" enc="); Serial.println(targetEnc);
  } else {
    Serial.print("[WiFi] Target SSID NOT seen: "); Serial.println(WIFI_SSID);
    Serial.println("[WiFi] If it is hidden or on channel 12/13, consider revealing SSID or changing to channels 1-11.");
  }
}

void TaskMQTT(void* pv) {
  Serial.println("TaskMQTT entered");
  Serial.print("Connecting WiFi "); Serial.println(WIFI_SSID);
  scanNetworksPrint();
  Serial.print("WiFiNINA firmware: "); Serial.println(WiFi.firmwareVersion());
  Serial.println("[WiFi] Skip disconnect, call begin directly");
  Serial.flush();
  // WiFi.disconnect();
  // vTaskDelay(pdMS_TO_TICKS(300));
  int s0 = WiFi.status();
  Serial.print("[WiFi] Status before begin="); Serial.print(s0); Serial.print(" ("); Serial.print(wifiStatusName(s0)); Serial.println(")");
  Serial.println("[WiFi] Calling WiFi.begin...");
  Serial.flush();
  int beginRet = WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("[WiFi] WiFi.begin ret="); Serial.println(beginRet);
  Serial.flush();
  int initStatus = WiFi.status();
  Serial.print("[WiFi] Initial status="); Serial.print(initStatus); Serial.print(" ("); Serial.print(wifiStatusName(initStatus)); Serial.println(")");
  Serial.flush();

  unsigned long t0 = millis();
  unsigned long lastStatusLog = millis();
  unsigned long lastRescan = millis();
  Serial.println("[WiFi] Entering connect wait loop...");
  Serial.flush();
  while (WiFi.status() != WL_CONNECTED) {
    vTaskDelay(pdMS_TO_TICKS(500));
    int sNow = WiFi.status();
    if (millis() - lastStatusLog > 3000) {
      Serial.print(" status="); Serial.print(sNow);
      Serial.print(" ("); Serial.print(wifiStatusName(sNow)); Serial.println(")");
      Serial.flush();
      lastStatusLog = millis();
    }
    if (sNow == WL_NO_SSID_AVAIL && (millis() - lastRescan) > 10000) {
      Serial.println("\n[WiFi] NO_SSID, rescan...");
      scanNetworksPrint();
      Serial.flush();
      lastRescan = millis();
    }
    if (millis() - t0 > 20000) {
      Serial.println("\n[WiFi] Timeout, retry begin...");
      beginRet = WiFi.begin(WIFI_SSID, WIFI_PASS);
      Serial.print("[WiFi] retry WiFi.begin ret="); Serial.println(beginRet);
      Serial.flush();
      t0 = millis();
    }
  }
  Serial.print("\nWiFi OK. IP="); Serial.println(WiFi.localIP());
  Serial.print("[WiFi] FreeHeap="); Serial.println(xPortGetFreeHeapSize());
  Serial.flush();

  mqttClient.setId(String("OpenAPS-") + TEAM);
  mqttClient.setUsernamePassword(MQTT_USER, MQTT_PASSWD);
  mqttClient.onMessage(onMqttMessage);

  Serial.print("Connecting MQTT "); Serial.print(MQTT_HOST); Serial.print(":"); Serial.println(MQTT_PORT);
  if (!mqttClient.connect(MQTT_HOST, MQTT_PORT)) { Serial.println("MQTT connect failed"); vTaskDelete(NULL); }
  Serial.println("MQTT OK");
  Serial.print("[MQTT] FreeHeap(before sub)="); Serial.println(xPortGetFreeHeapSize());

  int sub1 = mqttClient.subscribe(TOPIC_CGM_SUB, 1);
  int sub2 = mqttClient.subscribe(ATTR_RESP, 1);
  int subAll = mqttClient.subscribe(PFX + "/#", 1);
  int subRaw = mqttClient.subscribe(TOPIC_CGM_RAW, 1);
  int subSelf = mqttClient.subscribe(SELFTEST, 1);
  Serial.print("[MQTT] Subscribed CGM ret="); Serial.println(sub1);
  Serial.print("[MQTT] Subscribed ATTR ret="); Serial.println(sub2);
  Serial.print("[MQTT] Subscribed ALL ret="); Serial.println(subAll);
  Serial.print("[MQTT] Subscribed RAW ret="); Serial.println(subRaw);
  Serial.print("[MQTT] Subscribed SELF ret="); Serial.println(subSelf);
  Serial.print("[MQTT] Topics: CGM="); Serial.print(TOPIC_CGM_SUB);
  Serial.print(" ATTR="); Serial.print(ATTR_RESP);
  Serial.print(" ALL="); Serial.print(PFX + "/#");
  Serial.print(" RAW="); Serial.print(TOPIC_CGM_RAW);
  Serial.print(" SELF="); Serial.println(SELFTEST);
  Serial.print("[MQTT] FreeHeap(after sub)="); Serial.println(xPortGetFreeHeapSize());

  // publish self-test message to verify onMessage+poll path
  mqttClient.beginMessage(SELFTEST, false, 1);
  mqttClient.print("ping");
  mqttClient.endMessage();
  Serial.println("[MQTT] Selftest published 'ping'");
  mqttClient.beginMessage(ATTR_REQ);
  mqttClient.print("{\"request\":\"PatientProfile\"}");
  mqttClient.endMessage();
  Serial.print("[ATTR] Req sent. FreeHeap="); Serial.println(xPortGetFreeHeapSize());

  if (hTaskOpenAPS == NULL) {
    BaseType_t r3 = xTaskCreate(TaskOpenAPS, "TaskOpenAPS", 512, NULL, 1, &hTaskOpenAPS);
    Serial.print("TaskOpenAPS create (deferred) ret="); Serial.println((int)r3);
    Serial.print("Free heap after OpenAPS(deferred)="); Serial.println(xPortGetFreeHeapSize());
  }

  Serial.println("TaskMQTT started main loop");
  unsigned long lastPollLog = millis();
  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[WiFi] Lost, reconnecting...");
      int br = WiFi.begin(WIFI_SSID, WIFI_PASS);
      Serial.print("[WiFi] reconnect WiFi.begin ret="); Serial.println(br);
      Serial.flush();
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }
    if (!mqttClient.connected()) {
      Serial.println("[MQTT] Reconnecting...");
      if (!mqttClient.connect(MQTT_HOST, MQTT_PORT)) {
        Serial.println("[MQTT] reconnect failed");
        vTaskDelay(pdMS_TO_TICKS(500));
        continue;
      }
      Serial.println("[MQTT] Reconnected"); 
      mqttClient.subscribe(TOPIC_CGM_SUB, 1);
      mqttClient.subscribe(ATTR_RESP, 1);
      mqttClient.subscribe(PFX + "/#", 1);
      mqttClient.subscribe(TOPIC_CGM_RAW, 1); // also listen to raw VP topic for diagnostics
    }
    mqttClient.poll();
    if (millis() - lastPollLog > 2000) {
      Serial.println("[MQTT] poll tick");
      Serial.flush();
      lastPollLog = millis();
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void TaskOpenAPS(void* pv) {
  Serial.println("TaskOpenAPS started");
  for (;;) {
    bool hasNew = false; float bg = 0.0f;
    if (stateMutex && xSemaphoreTake(stateMutex, (TickType_t)10) == pdTRUE) {
      hasNew = newBGData;
      if (hasNew) { bg = current_BG; newBGData = false; }
      xSemaphoreGive(stateMutex);
    }
    if (hasNew) {
      float insulin_rate = (bg > 120.0f) ? 0.5f : 0.0f; // M1 placeholder
      publishBasal(insulin_rate);
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void TaskHeartbeat(void* pv) {
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.println("TaskHeartbeat started");
  int cnt = 0;
  for (;;) {
    digitalWrite(LED_BUILTIN, HIGH);
    vTaskDelay(pdMS_TO_TICKS(500));
    digitalWrite(LED_BUILTIN, LOW);
    vTaskDelay(pdMS_TO_TICKS(500));
    Serial.println("HB tick");
    Serial.flush();
    cnt++;
    if ((cnt % 4) == 0) { // every ~2s
      UBaseType_t hwmHB   = uxTaskGetStackHighWaterMark(NULL);
      UBaseType_t hwmMQTT = hTaskMQTT   ? uxTaskGetStackHighWaterMark(hTaskMQTT)   : 0;
      UBaseType_t hwmAPS  = hTaskOpenAPS? uxTaskGetStackHighWaterMark(hTaskOpenAPS): 0;
      size_t freeHeap = xPortGetFreeHeapSize();
      Serial.print("[RTOS] HWM HB="); Serial.print(hwmHB);
      Serial.print(" MQTT="); Serial.print(hwmMQTT);
      Serial.print(" APS="); Serial.print(hwmAPS);
      Serial.print(" FreeHeap="); Serial.println(freeHeap);
      Serial.flush();
    }
  }
}

void setup() {
  Serial.begin(115200);
  unsigned long s = millis();
  while (!Serial && (millis() - s) < 20000) { /* wait up to 20 seconds */ }
  pinMode(LED_BUILTIN, OUTPUT);
  for (int i = 0; i < 4; i++) {
    digitalWrite(LED_BUILTIN, HIGH);
    delay(150);
    digitalWrite(LED_BUILTIN, LOW);
    delay(150);
  }
  Serial.println("OpenAPS (M1, FreeRTOS) start");
  Serial.print("FreeRTOS kernel version: "); Serial.println(tskKERNEL_VERSION_NUMBER);
  Serial.println("Serial ready, creating resources...");
  Serial.flush();

  stateMutex = xSemaphoreCreateMutex();
  if (!stateMutex) { Serial.println("Mutex create FAILED"); } else { Serial.println("Mutex OK"); }
  Serial.print("Free heap after mutex="); Serial.println(xPortGetFreeHeapSize());

  // Reduce stacks to fit heap budget (sizes are in words)
  BaseType_t r1 = xTaskCreate(TaskHeartbeat, "TaskHeartbeat", 384, NULL, 2, &hTaskHeartbeat);
  Serial.print("TaskHeartbeat create ret="); Serial.println((int)r1);
  Serial.println(r1 == pdPASS ? "TaskHeartbeat created" : "TaskHeartbeat create FAILED");
  Serial.print("Free heap after HB="); Serial.println(xPortGetFreeHeapSize());

  BaseType_t r2 = xTaskCreate(TaskMQTT, "TaskMQTT", 1536, NULL, 1, &hTaskMQTT);
  Serial.print("TaskMQTT create ret="); Serial.println((int)r2);
  Serial.println(r2 == pdPASS ? "TaskMQTT created" : "TaskMQTT create FAILED");
  Serial.print("Free heap after MQTT="); Serial.println(xPortGetFreeHeapSize());

  // Defer TaskOpenAPS creation until MQTT is up to reduce peak memory pressure
  Serial.println("Deferring TaskOpenAPS creation until after MQTT connect...");
  Serial.print("Free heap before scheduler="); Serial.println(xPortGetFreeHeapSize());

  Serial.println("Starting scheduler...");
  Serial.flush();
  vTaskStartScheduler();
  Serial.println("Scheduler returned! (insufficient heap?)");
}

// FreeRTOS diagnostic hooks (disabled to avoid duplicate definitions with library)
#if 0
extern "C" void vApplicationMallocFailedHook(void) {
  Serial.println("[FreeRTOS] Malloc failed hook");
  pinMode(LED_BUILTIN, OUTPUT);
  for (;;) { digitalWrite(LED_BUILTIN, HIGH); delay(80); digitalWrite(LED_BUILTIN, LOW); delay(80); }
}
extern "C" void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
  Serial.print("[FreeRTOS] Stack overflow: "); Serial.println(pcTaskName);
  pinMode(LED_BUILTIN, OUTPUT);
  for (;;) { digitalWrite(LED_BUILTIN, HIGH); delay(200); digitalWrite(LED_BUILTIN, LOW); delay(200); }
}
#endif

void loop() {
  // 在某些 FreeRTOS 端口，loop() 仍会运行于空闲上下文；避免误导输出
  vTaskDelay(pdMS_TO_TICKS(1000));
}
