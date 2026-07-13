# Hardware Wiring

## ESP32-S3 to INMP441

| INMP441 pin | ESP32-S3 connection | Purpose |
|---|---:|---|
| `VDD` | `3.3V` | Microphone power |
| `GND` | `GND` | Common ground |
| `SCK` / `BCLK` | GPIO `14` | I2S bit clock |
| `WS` / `LRCL` | GPIO `15` | I2S word-select clock |
| `SD` / `DOUT` | GPIO `16` | Microphone data into ESP32-S3 |
| `L/R` | `GND` | Select left channel |

Use 3.3 V only. Do not connect the INMP441 power pin to 5 V.

Keep the microphone wiring short and place a 0.1 µF decoupling capacitor near the module if the power supply is noisy.

## If the microphone reports an error

1. Confirm `VDD` is 3.3 V and the grounds are common.
2. Confirm `L/R` is connected to GND.
3. Confirm GPIO 14, 15 and 16 match the wiring.
4. If `L/R` is tied to 3.3 V instead, set `AURASYNC_MIC_USE_LEFT_CHANNEL` to `0`.
5. Check that Arduino IDE uses ESP32 by Espressif Systems 3.x.
6. Open Serial Monitor at 115200 baud and restart the board.

Pins can be changed in `aurasync_config.h`.

