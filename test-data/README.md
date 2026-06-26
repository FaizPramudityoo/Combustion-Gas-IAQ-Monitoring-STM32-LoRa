# Test Data

This folder contains raw data collected during LoRa communication testing for the combustion gas and indoor air quality monitoring system.

## lora-test/lora_test1_partial_packets8-79.csv

**Important — this is a partial excerpt, not the complete dataset.**

This file contains 72 packets (packet IDs 5385–5456, corresponding to packets 8 through 79 of an expected 600) captured from a single PlatformIO serial monitor session during Test Position 1 (same room, line-of-sight). It does not cover the full 30-minute / ~585-packet test window for this position, and does not include data for the other four test positions.

The complete summary results for all five test positions — packet delivery rate, RSSI, SNR, and link margin — are reported in the thesis. Those summary figures were read directly from the receiver firmware's automated Test Mode result screen at the end of each 30-minute window, rather than reconstructed from this raw log file.

This CSV is included to give a transparent, real example of the raw per-packet data format produced by the system (timestamp, packet ID, RSSI, SNR, and the corresponding sensor readings).

## Columns

| Column | Description |
|---|---|
| packet_id | Sequential packet identifier assigned by the transmitter firmware |
| timestamp_ms | Milliseconds since receiver boot, at time of packet reception |
| test_number | Test Mode position number (1–5) |
| packet_num | Packet count within the current test window, as tracked by the receiver |
| packet_total_expected | Nominal expected packet count for a full window (600, based on a 3-second interval; see thesis Section 4.2.2 and 6.2 for the discussion of the measured ~3,080 ms actual interval and the resulting ~585-packet expected count) |
| lost_count_running_total | Running count of packets lost so far in this window, as detected via gaps in the packet ID sequence |
| rssi_dbm | Received Signal Strength Indicator, in dBm |
| snr_db | Signal-to-Noise Ratio, in dB |
| co_ppm | MQ-7 carbon monoxide reading, in ppm (response/trend indicator only — not a validated absolute accuracy measurement; see thesis Section 3.1.2 and Chapter VII) |
| co2eq_ppm | MQ-135 CO₂-equivalent reading, in ppm (response/trend indicator only — not a validated absolute accuracy measurement; see thesis Section 3.1.3 and Chapter VII) |
| temperature_c | DHT22 temperature reading, in °C |
| humidity_rh | DHT22 relative humidity reading, in %RH |
| status | "received" or "lost" (lost packets are inserted as placeholder rows wherever a gap is detected in the packet ID sequence, with other fields left blank) |

## Source

Converted from the raw PlatformIO serial monitor log using the script at `scripts/log_to_csv.py` in this repository.
