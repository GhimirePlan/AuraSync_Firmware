# Firmware Verification Report

## Completed checks

| Check | Result |
|---|---|
| Three semantic CNN outputs only | Pass |
| LOUD removed from CNN | Pass |
| SPIKE removed from CNN | Pass |
| DSP-only loud/spike logic | Pass |
| Continuous fusion score | Pass |
| State hysteresis and confirmation timing | Implemented |
| Raw-audio web endpoints | None present |
| Hard-coded personal Wi-Fi credentials | None present |
| Labelled showcase simulation | Pass |
| C++ model inference vs training implementation | Pass |
| Firmware feature extraction vs training implementation | Pass |
| Host-side C++ syntax compilation | Pass |
| Model independent test evaluation | Pass |

## Numerical parity

Three golden model inputs were compared between NumPy training inference and the C++ firmware inference.

- Maximum probability difference: below `0.000001`
- Acceptance tolerance: `0.000010`

Firmware feature extraction was compared with the training feature extractor using an actual verified WAV file.

- Mean absolute feature difference: `5.74e-08`
- Maximum absolute feature difference: `6.26e-06`
- Acceptance tolerance: `1.00e-03`

Golden inputs and expected outputs are supplied in `tests/golden_vectors.json`.

## Static build check

The complete `.ino` and headers passed a GNU C++17 host-side syntax build with Arduino/ESP32 API stubs. The embedded model header also passed a native C++ executable parity test.

The current execution environment does not contain the Arduino ESP32 cross-compiler or physical ESP32-S3/INMP441 hardware. Therefore, an actual Arduino board build, flash, microphone test, latency measurement, power measurement and long-duration soak test remain hardware acceptance steps.

## Required hardware acceptance

Before describing the complete device as hardware-verified:

1. Compile using ESP32 by Espressif Systems 3.x.
2. Flash an ESP32-S3 N8R8.
3. Confirm the INMP441 channel and sample amplitude.
4. Compare estimated LAeq against a reference meter at multiple levels.
5. Measure inference time, free heap and I2S errors on the dashboard.
6. Run continuously for at least 24 hours.
7. Test Wi-Fi disconnect/reconnect and dashboard access.
8. Confirm through network inspection that no raw audio leaves the device.
9. Record field confusion matrix and state false-warning/missed-warning results.
10. Complete the planned human five-point comfort-rating study.

