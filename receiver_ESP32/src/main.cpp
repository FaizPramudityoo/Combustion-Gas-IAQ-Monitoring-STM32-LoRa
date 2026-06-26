#include <WiFi.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// MODE SELECT — uncomment only ONE, or none for normal monitoring
//#define CALIBRATION_MODE
//#define TEST_MODE

// Ubidots Config 
#include "secrets.h"
#define UBIDOTS_DEVICE  "gasmonitoring"
#define UBIDOTS_URL     "http://industrial.api.ubidots.com/api/v1.6/devices/" UBIDOTS_DEVICE "/"
#define UBIDOTS_INTERVAL_MS  180000UL



// Calibration Ratios
#define CLEAN_AIR_RATIO_MQ7   27.0f
#define CLEAN_AIR_RATIO_MQ135  3.6f

// Pin Definitions
const int ss   =  5;
const int rst  = 14;
const int dio0 =  4;
#define BAT_PIN   34
#define OLED_SDA  21
#define OLED_SCL  22

Adafruit_SSD1306 display(128, 64, &Wire, -1);

//  Battery 
#define BAT_DIVIDER  0.5f
#define BAT_SAMPLES  16

// Safety Thresholds 
#define CO_WARN     9.0f
#define CO_DANGER   35.0f
#define CO2_WARN    1000.0f
#define CO2_DANGER  2000.0f
#define TEMP_LOW    10.0f
#define TEMP_HIGH   35.0f
#define HUM_LOW     20.0f
#define HUM_HIGH    80.0f
#define NO_SIGNAL_MS 15000UL

// WiFi Retry
#define WIFI_RETRY_MS 30000UL

// Global State 
float         g_val1          = 0;
float         g_val2          = 0;
float         g_temp          = 0;
float         g_hum           = 0;
int           g_rssi          = 0;
uint32_t      g_pktID         = 0;
bool          g_gotData       = false;
bool          g_wifiConnected = false; 
unsigned long g_lastPacketMs  = 0;
unsigned long g_lastWifiTryMs = 0;
unsigned long g_lastUbidotsMs = 0;

// Test Mode Config 
#ifdef TEST_MODE
#define TEST_WINDOW_MS  1800000UL
#define RESULT_HOLD_MS   15000UL
#define TX_INTERVAL_MS    3000UL
#define MAX_EXPECTED         600

struct TestResult {
  int   received;
  int   expected;
  int   lost;
  float successRate;
  int   rssiAvg;
  int   rssiMin;
  int   rssiMax;
  float snrAvg;       // ★ SNR — TEST_MODE only
  float snrMin;       // ★ SNR — TEST_MODE only
  float snrMax;       // ★ SNR — TEST_MODE only
};

int           g_testNumber   = 1;
int           g_pktsReceived = 0;
long          g_rssiSum      = 0;
int           g_rssiCount    = 0;
int           g_rssiMin      = 0;
int           g_rssiMax      = -200;
bool          g_showResult   = false;
bool          g_testStarted  = false;
unsigned long g_windowStart  = 0;
unsigned long g_resultStart  = 0;
TestResult    g_lastResult;

// ★ SNR — TEST_MODE only, not used in normal/calibration mode
float g_snr      = 0;
float g_snrSum    = 0;
int   g_snrCount  = 0;
float g_snrMin    = 0;
float g_snrMax    = -200;

uint32_t g_firstSeqID   = 0;
uint32_t g_lastSeqID    = 0;
bool     g_seqInit      = false;
int      g_seqLostCount = 0;
#endif


// WiFi Event Handler — fires instantly on connect/disconnect
void WiFiEvent(WiFiEvent_t event) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      g_wifiConnected = true;
      Serial.print("WiFi connected! IP: ");
      Serial.println(WiFi.localIP());
      break;

    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      g_wifiConnected = false;
      Serial.println("WiFi disconnected!");
      break;

    case ARDUINO_EVENT_WIFI_STA_LOST_IP:
      g_wifiConnected = false;
      Serial.println("WiFi lost IP!");
      break;

    default:
      break;
  }
}

// ════════════════════════════════════════════════════════════════════════════
// Battery
// ════════════════════════════════════════════════════════════════════════════
float readBatteryVoltage() {
  long raw = 0;
  for (int i = 0; i < BAT_SAMPLES; i++) {
    raw += analogRead(BAT_PIN);
    delay(2);
  }
  float v_adc = ((float)(raw / BAT_SAMPLES) / 4095.0f) * 3.3f;
  return v_adc / BAT_DIVIDER;
}

int batteryPercent(float v) {
  if (v >= 4.20f) return 100;
  if (v >= 4.00f) return (int)(80 + (v - 4.00f) / 0.20f * 20);
  if (v >= 3.80f) return (int)(50 + (v - 3.80f) / 0.20f * 30);
  if (v >= 3.60f) return (int)(20 + (v - 3.60f) / 0.20f * 30);
  if (v >= 3.00f) return (int)(0  + (v - 3.00f) / 0.60f * 20);
  return 0;
}


// Ubidots Upload — no SNR here (kept to TEST_MODE only)
void sendToUbidots(float co, float co2, float temp, float hum, int bat) {
  if (!g_wifiConnected) {
    Serial.println("Ubidots: skipped — no WiFi");
    return;
  }

  Serial.println("Ubidots: sending...");

  HTTPClient http;
  http.begin(UBIDOTS_URL);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-Auth-Token",  UBIDOTS_TOKEN);
  http.setTimeout(5000);

  String payload = "{";
  payload += "\"co\":"          + String(co,   1) + ",";
  payload += "\"co2\":"         + String(co2,  0) + ",";
  payload += "\"temperature\":" + String(temp, 1) + ",";
  payload += "\"humidity\":"    + String(hum,  1) + ",";
  payload += "\"battery\":"     + String(bat)     + ",";
  payload += "\"rssi\":"        + String(g_rssi);
  payload += "}";

  Serial.print("Payload: "); Serial.println(payload);

  int code = http.POST(payload);

  if (code == 200 || code == 201) {
    Serial.printf("Ubidots POST → %d (SUCCESS)\n", code);
  } else {
    Serial.printf("Ubidots POST → %d (FAILED)\n", code);
    if (code == 401) Serial.println("  → Wrong token!");
    if (code == 404) Serial.println("  → Wrong device label!");
    if (code ==  -1) Serial.println("  → Connection error");
  }

  http.end();
}

// ════════════════════════════════════════════════════════════════════════════
// WiFi Background Handler
// ════════════════════════════════════════════════════════════════════════════
void handleWiFi() {
  if (millis() - g_lastWifiTryMs < WIFI_RETRY_MS) return;
  g_lastWifiTryMs = millis();

  if (!g_wifiConnected) {
    Serial.println("WiFi: reconnecting...");
    WiFi.begin(ssid, pass);
  }
}

// ════════════════════════════════════════════════════════════════════════════
// Threshold Helpers
// ════════════════════════════════════════════════════════════════════════════
#ifndef CALIBRATION_MODE
#ifndef TEST_MODE
int statusCO(float v)   { return v >= CO_DANGER              ? 2 : v >= CO_WARN  ? 1 : 0; }
int statusCO2(float v)  { return v >= CO2_DANGER             ? 2 : v >= CO2_WARN ? 1 : 0; }
int statusTemp(float v) { return (v >= TEMP_HIGH || v <= TEMP_LOW) ? 1 : 0; }
int statusHum(float v)  { return (v >= HUM_HIGH  || v <= HUM_LOW)  ? 1 : 0; }

bool anyAlert() {
  return statusCO(g_val1)   >= 1 || statusCO2(g_val2)  >= 1 ||
         statusTemp(g_temp) >= 1 || statusHum(g_hum)   >= 1;
}
#endif
#endif

// ════════════════════════════════════════════════════════════════════════════
// OLED Helpers
// ════════════════════════════════════════════════════════════════════════════
void drawBatteryIcon(int x, int y, int pct) {
  display.drawRect(x, y, 18, 10, WHITE);
  display.fillRect(x + 18, y + 3, 2, 4, WHITE);
  int w = (int)(14.0f * pct / 100.0f);
  if (w > 0) display.fillRect(x + 2, y + 2, w, 6, WHITE);
}

// Two-state WiFi icon — event-driven, always accurate
void drawWiFiIcon(int x, int y) {
  if (g_wifiConnected) {
    display.fillRect(x,     y + 5, 2, 2, WHITE);
    display.fillRect(x + 3, y + 3, 2, 4, WHITE);
    display.fillRect(x + 6, y + 1, 2, 6, WHITE);
  } else {
    display.drawLine(x,     y,     x + 7, y + 7, WHITE);
    display.drawLine(x + 1, y,     x + 7, y + 6, WHITE);
    display.drawLine(x + 7, y,     x,     y + 7, WHITE);
    display.drawLine(x + 6, y,     x,     y + 6, WHITE);
  }
}

const char* signalQuality(int rssi) {
  if (rssi > -70)  return "Excellent";
  if (rssi > -85)  return "Good";
  if (rssi > -100) return "Fair";
  return "Poor";
}

// ════════════════════════════════════════════════════════════════════════════
// CALIBRATION MODE DISPLAY
// ════════════════════════════════════════════════════════════════════════════
#ifdef CALIBRATION_MODE
void updateOLED_Calibration(int batPct) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);

  display.setCursor(0, 0);
  display.print("CALIBRATION");
  char b[6]; sprintf(b, "%3d%%", batPct);
  display.setCursor(72, 0); display.print(b);
  drawBatteryIcon(100, 0, batPct);
  display.drawLine(0, 9, 127, 9, WHITE);

  if (g_gotData) {
    float r0_mq7   = g_val1 / CLEAN_AIR_RATIO_MQ7;
    float r0_mq135 = g_val2 / CLEAN_AIR_RATIO_MQ135;

    display.setCursor(0, 12);
    display.print("Rs_MQ7:   ");
    display.print(g_val1, 2); display.println(" k");
    display.print("Rs_MQ135: ");
    display.print(g_val2, 2); display.println(" k");
    display.print("Temp: ");
    display.print(g_temp, 1); display.println(" C");
    display.print("Hum:  ");
    display.print(g_hum,  1); display.println(" %");
    display.drawLine(0, 42, 127, 42, WHITE);

    display.setTextColor(BLACK, WHITE);
    display.setCursor(0, 44);
    display.print("R0_MQ7:   "); display.println(r0_mq7,   3);
    display.print("R0_MQ135: "); display.println(r0_mq135, 3);
    display.setTextColor(WHITE);
  } else {
    display.setCursor(10, 28);
    display.print("Waiting for data...");
  }
  display.display();
}
#endif

// ════════════════════════════════════════════════════════════════════════════
// TEST MODE LOGIC + DISPLAY
// ════════════════════════════════════════════════════════════════════════════
#ifdef TEST_MODE
void handleTestMode() {
  unsigned long now = millis();

  if (g_showResult) {
    if (now - g_resultStart >= RESULT_HOLD_MS) {
      g_showResult   = false;
      g_pktsReceived = 0;
      g_rssiSum      = 0;
      g_rssiCount    = 0;
      g_rssiMin      = 0;
      g_rssiMax      = -200;
      g_snrSum       = 0;
      g_snrCount     = 0;
      g_snrMin       = 0;
      g_snrMax       = -200;
      g_windowStart  = now;

      g_seqInit      = false;
      g_seqLostCount = 0;
      g_firstSeqID   = 0;
      g_lastSeqID    = 0;

      if (g_testNumber > 5) g_testNumber = 1;
    }
    return;
  }

  if (!g_testStarted) return;

  if (now - g_windowStart >= TEST_WINDOW_MS) {
    int seqSpan = g_seqInit ? (int)(g_lastSeqID - g_firstSeqID + 1) : 0;

    g_lastResult.received    = g_pktsReceived;
    g_lastResult.expected    = seqSpan;
    g_lastResult.lost        = g_seqLostCount;
    g_lastResult.successRate = seqSpan > 0
                               ? g_pktsReceived * 100.0f / seqSpan
                               : 0;
    g_lastResult.rssiAvg     = g_rssiCount > 0 ? (int)(g_rssiSum / g_rssiCount) : 0;
    g_lastResult.rssiMin     = g_rssiMin;
    g_lastResult.rssiMax     = g_rssiMax;
    g_lastResult.snrAvg      = g_snrCount > 0 ? (g_snrSum / g_snrCount) : 0;
    g_lastResult.snrMin      = g_snrMin;
    g_lastResult.snrMax      = g_snrMax;
    g_testNumber++;
    g_showResult  = true;
    g_resultStart = now;
  }
}

void updateOLED_TestLive(int batPct) {
  unsigned long elapsed  = g_testStarted ? millis() - g_windowStart : 0;
  int remaining_s        = g_testStarted
                           ? max(0UL, (TEST_WINDOW_MS - elapsed)) / 1000
                           : (int)(TEST_WINDOW_MS / 1000);

  int seqSpan = g_seqInit ? (int)(g_lastSeqID - g_firstSeqID + 1) : 0;
  int loss    = g_seqLostCount;
  float rate  = seqSpan > 0 ? g_pktsReceived * 100.0f / seqSpan : 0;
  int rssiAvg = g_rssiCount > 0 ? (int)(g_rssiSum / g_rssiCount) : 0;
  float snrAvg = g_snrCount > 0 ? (g_snrSum / g_snrCount) : 0;

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);

  display.setCursor(0, 0);
  char hdr[22]; sprintf(hdr, "LORA TEST [%d/5]", g_testNumber);
  display.print(hdr);
  char b[6]; sprintf(b, "%3d%%", batPct);
  display.setCursor(92, 0); display.print(b);
  display.drawLine(0, 9, 127, 9, WHITE);

  display.setCursor(0, 12);
  char row[22];

  if (!g_testStarted) {
    display.println("Place TX at pos.");
    display.println("Waiting for first");
    display.println("packet to start...");
  } else {
    sprintf(row, "Time:  %02d:%02d left",
            remaining_s / 60, remaining_s % 60);
    display.println(row);

    sprintf(row, "Recv:  %03d / %03d pkts",
            g_pktsReceived, MAX_EXPECTED);
    display.println(row);

    sprintf(row, "Loss:  %03d (ID gap)",
            loss);
    display.println(row);

    sprintf(row, "Rate:  %.1f%%", rate);
    display.println(row);
  }

  // ★ TEST_MODE only — RSSI + SNR shown together in footer
  display.drawLine(0, 51, 127, 51, WHITE);
  display.setCursor(0, 54);
  if (g_rssiCount > 0) {
    sprintf(row, "R:%ddBm S:%.1fdB", rssiAvg, snrAvg);
    display.print(row);
  } else {
    display.print("RSSI/SNR: waiting...");
  }
  display.display();
}

void updateOLED_TestResult(int batPct) {
  unsigned long timeLeft = (RESULT_HOLD_MS - (millis() - g_resultStart)) / 1000;
  TestResult&   r        = g_lastResult;

  display.clearDisplay();
  display.setTextSize(1);

  display.fillRect(0, 0, 128, 10, WHITE);
  display.setTextColor(BLACK);
  display.setCursor(2, 1);
  char hdr[22];
  sprintf(hdr, "RESULT [Test %d/5]", g_testNumber - 1);
  display.print(hdr);
  display.setTextColor(WHITE);
  display.drawLine(0, 10, 127, 10, WHITE);

  display.setCursor(0, 13);
  char row[22];

  sprintf(row, "Recv:%03d/%03d Loss:%03d",
          r.received, r.expected, r.lost);
  display.println(row);

  sprintf(row, "Rate:  %.1f%%", r.successRate);
  display.println(row);

  sprintf(row, "RSSI:  %d dBm", r.rssiAvg);
  display.println(row);

  // ★ TEST_MODE only — SNR shown on result screen
  sprintf(row, "SNR:   %.1f dB", r.snrAvg);
  display.println(row);

  sprintf(row, "Qual:  %s", signalQuality(r.rssiAvg));
  display.println(row);

  display.drawLine(0, 54, 127, 54, WHITE);
  display.setCursor(0, 56);
  sprintf(row, "Next in: %lds", timeLeft);
  display.print(row);

  display.display();
}
#endif

// ════════════════════════════════════════════════════════════════════════════
// NORMAL MODE DISPLAY
// ════════════════════════════════════════════════════════════════════════════
#ifndef CALIBRATION_MODE
#ifndef TEST_MODE
void drawSensorRow(int y, const char* lbl, float val,
                   const char* unit, int s) {
  char buf[10];
  if      (val < 100.0f)   sprintf(buf, "%.1f", val);
  else if (val < 10000.0f) sprintf(buf, "%.0f", val);
  else                     sprintf(buf, "HIGH");

  display.setTextColor(WHITE);
  display.setCursor(0, y);
  display.print(lbl);
  display.print(buf);
  display.print(unit);

  const char* tag = s == 2 ? "DANGER" : s == 1 ? "WARN" : "SAFE";
  int tx = 127 - (int)strlen(tag) * 6;
  display.setCursor(tx, y);

  if (s == 2) {
    display.fillRect(tx - 1, y - 1, (int)strlen(tag) * 6 + 2, 9, WHITE);
    display.setTextColor(BLACK);
    display.print(tag);
    display.setTextColor(WHITE);
  } else {
    display.print(tag);
  }
}

void updateOLED_Normal(int batPct) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);

  // ── Header ───────────────────────────────────────────────────
  display.setCursor(0, 0);
  display.print("Gas Mon");

  if (anyAlert() && ((millis() / 500) % 2 == 0)) {
    display.setCursor(42, 0);
    display.print("!");
  }

  drawWiFiIcon(44, 1);

  char b[6]; sprintf(b, "%3d%%", batPct);
  display.setCursor(58, 0); display.print(b);
  drawBatteryIcon(90, 0, batPct);
  display.drawLine(0, 9, 127, 9, WHITE);

  // ── Sensor Rows ───────────────────────────────────────────────
  if (g_gotData) {
    drawSensorRow(12, "CO:  ", g_val1, "p ", statusCO(g_val1));
    drawSensorRow(22, "CO2: ", g_val2, "p ", statusCO2(g_val2));
    drawSensorRow(32, "Tmp: ", g_temp, "C ", statusTemp(g_temp));
    drawSensorRow(42, "Hum: ", g_hum,  "% ", statusHum(g_hum));
  } else {
    display.setCursor(20, 28);
    display.print("Waiting for data...");
  }

  // ── Footer — RSSI only (no SNR in normal mode) ─────────────────
  display.drawLine(0, 51, 127, 51, WHITE);
  display.setCursor(0, 54);

  bool noSig = g_gotData && (millis() - g_lastPacketMs > NO_SIGNAL_MS);
  if (noSig) {
    display.print("!!! NO SIGNAL !!!");
  } else {
    char r[22]; sprintf(r, "RSSI: %d dBm", g_rssi);
    display.print(r);
  }
  display.display();
}
#endif
#endif

// Setup
void setup() {
  Serial.begin(115200);
  analogSetPinAttenuation(BAT_PIN, ADC_11db);

  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED failed");
    while (1);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.println("Gas Mon v1.0");
  display.println("Starting LoRa...");
  display.display();

  LoRa.setPins(ss, rst, dio0);
  if (!LoRa.begin(433E6)) {
    display.println("LoRa FAILED!");
    display.println("Check wiring.");
    display.display();
    while (1);
  }

  // Explicit LoRa PHY parameters — must match transmitter exactly
  LoRa.setSpreadingFactor(7);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  LoRa.setSyncWord(0x12);

  display.println("LoRa OK.");

#ifdef CALIBRATION_MODE
  display.println("Mode: CALIBRATION");
  display.println("Go outdoors!");
  display.println("Wait 20 minutes.");
#elif defined(TEST_MODE)
  display.println("Mode: LORA TEST");
  display.println("Place TX at pos 1");
  display.println("Starts on first");
  display.println("packet received.");
#else
  display.println("Mode: MONITORING");
#endif
  display.display();
  delay(1000);

#ifdef TEST_MODE
  WiFi.mode(WIFI_OFF);
  btStop();
  Serial.println("WiFi OFF — Test Mode");
#else
  display.println("WiFi: connecting...");
  display.display();

  WiFi.onEvent(WiFiEvent);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
  Serial.print("WiFi connecting");

  int tries = 0;
  while (!g_wifiConnected && tries < 30) {
    delay(500);
    Serial.print(".");
    tries++;
  }

  if (g_wifiConnected) {
    display.println("WiFi: connected!");
  } else {
    Serial.println("\nWiFi: offline, retrying later");
    display.println("WiFi: offline");
    display.println("(retrying in bg)");
  }
  display.display();
  delay(1000);
#endif
}

// ════════════════════════════════════════════════════════════════════════════
// Loop
// ════════════════════════════════════════════════════════════════════════════
void loop() {
#ifndef TEST_MODE
  handleWiFi();
#endif

  // LoRa Receive
  int pktSize = LoRa.parsePacket();
  if (pktSize) {
    String incoming = "";
    while (LoRa.available()) incoming += (char)LoRa.read();

    int c0 = incoming.indexOf(',');
    int c1 = incoming.indexOf(',', c0 + 1);
    int c2 = incoming.indexOf(',', c1 + 1);
    int c3 = incoming.indexOf(',', c2 + 1);

    if (c0 > 0 && c1 > c0 && c2 > c1 && c3 > c2) {
      uint32_t pktID = (uint32_t) incoming.substring(0, c0).toInt();
      g_val1 = incoming.substring(c0 + 1, c1).toFloat();
      g_val2 = incoming.substring(c1 + 1, c2).toFloat();
      g_temp = incoming.substring(c2 + 1, c3).toFloat();
      g_hum  = incoming.substring(c3 + 1).toFloat();
      g_rssi         = LoRa.packetRssi();
      g_pktID        = pktID;
      g_gotData      = true;
      g_lastPacketMs = millis();

#ifdef TEST_MODE
      g_snr = LoRa.packetSnr();   // ★ SNR read only in TEST_MODE

      if (!g_testStarted) {
        g_testStarted = true;
        g_windowStart = millis();
        Serial.println("Test window started!");
      }
      if (!g_showResult) {
        g_pktsReceived++;
        g_rssiSum += g_rssi;
        g_rssiCount++;
        if (g_rssi < g_rssiMin || g_rssiMin == 0) g_rssiMin = g_rssi;
        if (g_rssi > g_rssiMax) g_rssiMax = g_rssi;

        g_snrSum += g_snr;
        g_snrCount++;
        if (g_snr < g_snrMin || g_snrMin == 0) g_snrMin = g_snr;
        if (g_snr > g_snrMax) g_snrMax = g_snr;

        if (!g_seqInit) {
          g_firstSeqID = pktID;
          g_lastSeqID  = pktID;
          g_seqInit    = true;
        } else if (pktID > g_lastSeqID) {
          int gap = (int)(pktID - g_lastSeqID - 1);
          if (gap > 0) g_seqLostCount += gap;
          g_lastSeqID = pktID;
        }

        Serial.printf("[%lu ms] Test %d | Pkt %d/%d | ID:%lu | Lost:%d | RSSI:%d | SNR:%.1f\n",
              millis(),
              g_testNumber, g_pktsReceived,
              MAX_EXPECTED, pktID, g_seqLostCount, g_rssi, g_snr);
      }
#endif

      Serial.printf("[%lu ms] RX: ID=%lu CO=%.1f CO2=%.0f T=%.1f H=%.1f RSSI=%d\n",
              millis(),
              pktID, g_val1, g_val2, g_temp, g_hum, g_rssi);
    }
  }

  // ── Display + Ubidots Refresh Every 1s ───────────────────────
  static unsigned long lastDisplay = 0;
  if (millis() - lastDisplay >= 1000) {
    lastDisplay = millis();
    float batV   = readBatteryVoltage();
    int   batPct = batteryPercent(batV);

#ifdef CALIBRATION_MODE
    updateOLED_Calibration(batPct);

#elif defined(TEST_MODE)
    handleTestMode();
    if (g_showResult) updateOLED_TestResult(batPct);
    else              updateOLED_TestLive(batPct);

#else
    updateOLED_Normal(batPct);

    if (g_wifiConnected &&
        g_gotData &&
        millis() - g_lastUbidotsMs >= UBIDOTS_INTERVAL_MS) {
      g_lastUbidotsMs = millis();
      sendToUbidots(g_val1, g_val2, g_temp, g_hum, batPct);
    }
#endif
  }

  yield();
}