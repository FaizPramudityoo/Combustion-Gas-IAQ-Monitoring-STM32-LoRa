# Combustion Gas and Indoor Air Quality Monitoring System (STM32 + LoRa)

This repository contains the firmware, hardware design files, and supporting test data for a two-node wireless system that monitors carbon monoxide, CO₂-equivalent air quality, temperature, and humidity in a residential indoor environment, using LoRa for wireless transmission and Ubidots for remote cloud monitoring.

## System overview

- **Transmitter node** (STM32F103C8T6 "Blue Pill"): reads MQ-7 (CO), MQ-135 (CO₂-equivalent), and DHT22 (temperature/humidity) sensors, and transmits packaged readings via a LoRa SX1278 module at 433 MHz.
- **Receiver node** (ESP32): receives LoRa packets, displays readings locally on an OLED screen, and forwards data to the Ubidots IoT platform over Wi-Fi for remote monitoring.

Full system design, methodology, and results are documented in the accompanying thesis.

## Repository structure

```
transmitter_STM32/      PlatformIO project for the STM32 transmitter firmware
receiver_ESP32/          PlatformIO project for the ESP32 receiver firmware
test-data/               Raw and processed LoRa communication test data
```

## Important note on sensor accuracy

The MQ-7 and MQ-135 sensors used in this project are evaluated for **response behavior and trend detection only**, not absolute measurement accuracy. No calibrated CO or CO₂ reference instrument was available during testing. See the thesis, Chapter III (Section 3.1.2–3.1.3) and Chapter VII, for full discussion of this limitation. This system is an academic research prototype and is **not a certified gas safety device**.

## Note on credentials

WiFi credentials and the Ubidots API token are intentionally excluded from this repository. The receiver firmware (`receiver_ESP32/src/main.cpp`) expects a `secrets.h` file in the same folder as `main.cpp`, which is not tracked by Git. To build this project yourself, create `receiver_ESP32/src/secrets.h` with your own credentials, following the structure referenced in `main.cpp`.

## Firmware

Both projects are built with [PlatformIO](https://platformio.org/). Open either folder in VS Code with the PlatformIO extension installed to build and upload.

| Folder | Target board | Key libraries |
|---|---|---|
| `transmitter_STM32/` | STM32F103C8T6 | LoRa, DHT |
| `receiver_ESP32/` | ESP32 | LoRa, WiFi, HTTPClient, Adafruit_GFX, Adafruit_SSD1306 |

## Test data

See `test-data/README.md` for details on what raw data is included and how it relates to the results reported in the thesis. Complete summary results for all five LoRa test positions are reported in the thesis (Chapter VI, Tables 6.6–6.9); the raw data included here is a partial representative excerpt, not a complete dataset — this is stated explicitly to avoid any impression that this repository contains more raw data than was actually retained during testing.

