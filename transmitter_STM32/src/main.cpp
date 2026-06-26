#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include <DHT.h>

// MODE SELECT — uncomment only ONE, or none for normal operation
// #define CALIBRATION_MODE

#define TEMP_OFFSET  -0.7f    
#define HUM_OFFSET   +0.26f    

// MQ Sensor R0 Values 
// Update after running CALIBRATION_MODE outdoors for 20 minutes
const float R0_MQ7   = 3.166f;
const float R0_MQ135 = 47.63f;

// Pin Definitions
#define DHTPIN     PA1
#define DHTTYPE    DHT22
#define MQ135_PIN  PA0
#define MQ7_PIN    PA2

const int csPin    = PB12;
const int resetPin = PA8;
const int irqPin   = PB0;

// Hardware Constants 
#define RL_MQ7     10.0f
#define RL_MQ135    1.0f
#define DIVIDER     1.606f
#define VCC         5.0f

// ADC Stability
#define ADC_SAMPLES    50
#define ADC_SETTLE_US  100

DHT dht(DHTPIN, DHTTYPE);

// ★ NEW — packet sequence ID, increments every send, used by receiver
//   to detect exact lost packets via gaps in the sequence
uint32_t g_packetID = 0;

// Stable ADC Read
float readADC_Stable(int pin) {
  analogRead(pin);
  delayMicroseconds(ADC_SETTLE_US);
  long sum = 0;
  for (int i = 0; i < ADC_SAMPLES; i++) {
    sum += analogRead(pin);
    delayMicroseconds(100);
  }
  return (float)sum / ADC_SAMPLES;
}

// Rs Calculation
float calculate_Rs(int pin, float rl) {
  float v_pin    = (readADC_Stable(pin) / 4095.0f) * 3.3f;
  float v_sensor = v_pin * DIVIDER;

  if (v_sensor < 0.005f) return 999.0f;
  if (v_sensor > 4.9f)   return 0.001f;

  return ((VCC - v_sensor) * rl) / v_sensor;
}

// PPM Calculation — PPM = a × (Rs/R0)^b
float calculate_PPM(float rs, float r0, float a, float b) {
  if (rs >= 999.0f || rs < 0.01f || r0 < 0.01f) return 0.0f;
  return a * pow((rs / r0), b);
}

// Setup
void setup() {
  analogReadResolution(12);
  dht.begin();

  LoRa.setPins(csPin, resetPin, irqPin);
  if (!LoRa.begin(433E6)) {
    while (1);
  }

  // ★ NEW — explicit LoRa PHY parameters, must match receiver exactly
  LoRa.setSpreadingFactor(7);        // SF7 — balance of range/speed
  LoRa.setSignalBandwidth(125E3);    // 125kHz — standard bandwidth
  LoRa.setCodingRate4(5);            // 4/5 — forward error correction
  LoRa.setSyncWord(0x12);            // private sync word — avoids interference

  LoRa.setTxPower(17);
  delay(2000);
}

// Loop
void loop() {
  // DHT22 Read + Offset
  float t = dht.readTemperature();
  float h = dht.readHumidity();

  if (isnan(t) || isnan(h)) {
    delay(2000);
    return;
  }

  t = t + TEMP_OFFSET;
  h = constrain(h + HUM_OFFSET, 0.0f, 100.0f);

  // MQ Rs Calculation 
  float rs_mq7   = calculate_Rs(MQ7_PIN,   RL_MQ7);
  float rs_mq135 = calculate_Rs(MQ135_PIN, RL_MQ135);

  // ★ NEW — increment packet ID before building this packet
  g_packetID++;

#ifdef CALIBRATION_MODE
  // Send Rs values so receiver OLED shows R0
  // ★ NEW — ID is now first field
  String packet = String(g_packetID) + "," +
                  String(rs_mq7,   2) + "," +
                  String(rs_mq135, 2) + "," +
                  String(t, 1)        + "," +
                  String(h, 1);
#else
  // Normal — send PPM values
  float co  = calculate_PPM(rs_mq7,   R0_MQ7,   99.04f,  -1.518f);
  float co2 = calculate_PPM(rs_mq135, R0_MQ135, 110.47f, -2.862f) + 400.0f;

  co  = constrain(co,  0.0f,    5000.0f);
  co2 = constrain(co2, 400.0f, 10000.0f);

  // ★ NEW — ID is now first field
  String packet = String(g_packetID) + "," +
                  String(co,  1) + "," +
                  String(co2, 0) + "," +
                  String(t,   1) + "," +
                  String(h,   1);
#endif

  // Transmit
  LoRa.beginPacket();
  LoRa.print(packet);
  LoRa.endPacket();

  delay(3000);    
}