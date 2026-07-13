# AuraSync ESP32-S3 INT8 Firmware

This firmware integrates the supplied `aurasync_3class_ds_cnn_full_int8.tflite` model with exact exported 49x64 log-Mel parameters, independent DSP LOUD/SPIKE detection, fusion scoring, and an offline web dashboard.

## Arduino setup

1. Install Arduino IDE 2.x.
2. Boards Manager: install **esp32 by Espressif Systems 3.x**.
3. Library Manager: install **TensorFlowLite_ESP32 by tanakamasayuki**.
4. Select **ESP32S3 Dev Module**. Enable PSRAM if your module has it.
5. Open `AuraSync_ESP32S3_INT8.ino`, compile, and upload.

## INMP441 wiring

| INMP441 | ESP32-S3 |
|---|---|
| VDD | 3.3V |
| GND | GND |
| SCK/BCLK | GPIO 5 |
| WS/LRCL | GPIO 4 |
| SD | GPIO 6 |
| L/R | GND (left channel) |

Change the three pin constants at the top of the `.ino` if required.

## Demonstration

After upload, connect a phone/laptop to Wi-Fi `AuraSync-INT8` using password `AuraSync2026`, then open `http://192.168.4.1`.

## Important validity note

The CNN model is notebook-validated and marked `APPROVED_FOR_PROTOTYPE`, not production-certified. `MIC_DBFS_TO_SPL` is a provisional microphone offset. Calibrate it beside a traceable/reference sound meter before presenting values as measured dBA. The current DSP level is RMS-derived estimated calibrated LAeq; full IEC-compliant A-weighting requires hardware/reference validation.

If model allocation fails, increase `TENSOR_ARENA_BYTES`. If `AllocateTensors()` reports a missing operator, use the serial error to add that operator to `initModel()` or temporarily use `tflite::AllOpsResolver` for diagnosis.

## Model results supplied by training package

- INT8 test accuracy: 89.82%
- INT8 macro-F1: 88.95%
- Input: INT8 `[1,49,64,1]`
- Output: stationary background, speech dominant, non-speech activity
- Model version: 2.0.0
