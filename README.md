# AuraSync ESP32-S3 Showcase Firmware

Complete firmware-only demonstration package for:

- ESP32-S3 N8R8
- INMP441 I2S microphone
- On-device DSP
- Learned three-class TinyCNN
- Acoustic Disturbance Score
- Stable state machine
- Responsive dashboard served directly by the ESP32-S3

No separate webserver, database, internet connection, TensorFlow library or external Arduino library is required.

## Quick start

1. Open `AuraSync_Showcase_Firmware.ino` in Arduino IDE.
2. Keep every supplied `.h` file in the same `AuraSync_Showcase_Firmware` folder.
3. Install/select **ESP32 by Espressif Systems 3.x**.
4. Select **ESP32S3 Dev Module**.
5. Wire the INMP441 as shown in `HARDWARE_WIRING.md`.
6. Upload the sketch.
7. Connect a phone or laptop to:
   - Wi-Fi: `AuraSync-Showcase`
   - Password: `AuraSync2026`
8. Open `http://192.168.4.1`.

The device always creates its own local access point. Existing Wi-Fi credentials are optional and can be entered in `aurasync_config.h`.

## Approved hybrid behaviour

| Layer | Firmware role |
|---|---|
| Fast DSP | A-weighted estimated LAeq, peak, crest factor, adaptive baseline, SNR, speech-band ratio, spectral flux, impulsiveness and microphone quality |
| TinyCNN | `stationary_background`, `speech_dominant`, `non_speech_activity` |
| Fusion | Continuous 0–100 Acoustic Disturbance Score |
| State machine | `SMOOTH`, `PRE_WARNING`, `WARNING`, `LOUD`, `RECOVERY`, `SPIKE`, `MIC_ERROR` |

`LOUD` and `SPIKE` are not CNN classes. They are generated only by calibrated DSP and the state machine.

## CNN included in this package

The package contains a real learned model, not random or placeholder weights.

- Architecture: Conv1D(16, kernel 3) → ReLU → Conv1D(16, kernel 3) → ReLU → global average/max pooling → Dense(3)
- Parameters: 1,632
- Input: 16 time frames × 17 level-invariant temporal/spectral features
- Training split: 8,750 verified clips
- Validation split: 1,876 verified clips
- Independent test split: 1,876 verified clips
- Test accuracy: 76.55%
- Test macro-F1: 75.74%
- Speech-dominant test F1: 90.91%

Complete class-wise results are in `tests/tinycnn_metrics.json` and `MODEL_CARD.md`.

## Privacy

Raw audio is temporarily processed in volatile memory. This release has no audio recording, audio playback, live-audio, WAV-download or audio-upload endpoint. Only low-dimensional measurements and class probabilities are displayed.

## Real and simulated data

The dashboard starts in live-microphone mode when the microphone initializes correctly. If the microphone fails, it can automatically enter a clearly marked showcase simulation.

The simulation:

- is visibly labelled in the dashboard;
- never claims to be a research measurement;
- can be toggled from the dashboard;
- is intended only for demonstrations without working microphone hardware.

Set `AURASYNC_ALLOW_SIMULATION` to `0` in `aurasync_config.h` for a research-only build.

## Important measurement limitation

The dashboard deliberately says **Estimated LAeq,1s**. The firmware includes a digital A-weighting filter and stored calibration offset, but the ESP32-S3/INMP441 combination is not automatically a standards-certified sound-level meter.

Calibrate it against a reference meter at the same microphone position before reporting physical sound levels. Multi-level laboratory comparison is still required for final research validation.

## Package files

```text
AuraSync_Showcase_Firmware/
├── AuraSync_Showcase_Firmware.ino   Main uploadable sketch
├── aurasync_config.h                Pins, Wi-Fi, calibration and thresholds
├── aurasync_dashboard.h             Embedded offline dashboard
├── aurasync_model.h                 TinyCNN inference implementation
├── aurasync_tinycnn_weights.h       Trained model weights
├── HARDWARE_WIRING.md
├── MODEL_CARD.md
├── UPLOAD_GUIDE.md
├── VERIFICATION_REPORT.md
└── tests/
    ├── golden_vectors.json
    └── tinycnn_metrics.json
```

