# Arduino IDE Upload Guide

## 1. Install the ESP32 board package

In Arduino IDE:

1. Open **File → Preferences**.
2. Add this Boards Manager URL if it is not already present:
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
3. Open **Tools → Board → Boards Manager**.
4. Install **ESP32 by Espressif Systems**, version 3.x.

## 2. Open the firmware

Open:

`AuraSync_Showcase_Firmware.ino`

Arduino IDE should show the `.ino` and all four supplied header tabs. Do not upload the `.ino` without its headers.

## 3. Recommended board options

Use:

| Arduino IDE option | Value |
|---|---|
| Board | ESP32S3 Dev Module |
| USB CDC On Boot | Enabled |
| CPU Frequency | 240 MHz |
| Flash Size | 8 MB |
| PSRAM | OPI PSRAM |
| Upload Mode | UART0 / Hardware CDC, according to the board |
| Upload Speed | 921600; use 460800 if unstable |

The firmware does not depend on PSRAM, but the N8R8 board should have OPI PSRAM enabled.

## 4. Optional Wi-Fi configuration

The local access point works without editing anything.

To also connect the ESP32-S3 to an existing network, edit:

```cpp
#define AURASYNC_WIFI_SSID "YOUR_WIFI_NAME"
#define AURASYNC_WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
```

Do not publish a source package that contains real credentials.

## 5. Upload and open the dashboard

1. Connect the ESP32-S3 with a data-capable USB cable.
2. Select the correct serial port.
3. Click **Upload**.
4. Open Serial Monitor at 115200 baud.
5. Connect a phone/laptop to `AuraSync-Showcase` using `AuraSync2026`.
6. Open `http://192.168.4.1`.

If connected to an existing Wi-Fi network, Serial Monitor also prints that network address. `http://aurasync.local` may work on networks supporting mDNS.

## 6. Calibration

1. Disable demo simulation.
2. Place a reference sound-level meter immediately beside the INMP441.
3. Use a stable reference sound.
4. Enter the meter reading in the dashboard calibration box.
5. Click **Calibrate dBA**.

The offset is stored in non-volatile preferences and survives restart.

One-point calibration is sufficient for a demonstration. Final research validation should compare multiple levels and acoustic conditions.

## Troubleshooting

- Dashboard unavailable: reconnect to `AuraSync-Showcase`, then open `192.168.4.1` directly.
- Continuous `MIC_ERROR`: check microphone wiring and left/right selection.
- Very high clipping: increase `AURASYNC_MIC_SHIFT_BITS` or reduce `AURASYNC_MIC_DIGITAL_GAIN`.
- Very low signal: reduce `AURASYNC_MIC_SHIFT_BITS` carefully, then recalibrate.
- Upload fails: hold the BOOT button while upload begins, or reduce upload speed.

