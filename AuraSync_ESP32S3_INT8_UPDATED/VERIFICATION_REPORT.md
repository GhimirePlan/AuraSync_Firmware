# Integration verification report

## Passed offline checks

- The supplied deployment ZIP and every file listed by its SHA-256 manifest passed integrity verification.
- Embedded model length is 77,384 bytes, matching the exported TFLite file.
- Class order is `stationary_background`, `speech_dominant`, `non_speech_activity`.
- Firmware input is 49 time frames x 64 Mel bands, INT8.
- Input quantization is scale `0.0420644507`, zero point `-33`.
- Output quantization is scale `0.00390625`, zero point `-128`.
- All 64 exported Mel means and standard deviations match the NPZ values (maximum decimal-rounding difference below `5e-9`).
- The generated Slaney-normalized Mel bank matches the exported 64x257 bank (maximum floating-point difference below `1.9e-9` in the independent check).
- The periodic Hann definition matches the exported 512-point window (maximum floating-point difference below `3.0e-8`).
- Model architecture inspection confirms Conv2D, depthwise Conv2D, global-average pooling, dense output, and final activation; the firmware resolver includes the corresponding TFLite Micro kernels.
- ZIP CRC/integrity test passed.

## Supplied model evaluation

- INT8 test accuracy: 0.8981876333
- INT8 balanced accuracy: 0.8860868378
- INT8 macro-F1: 0.8894624238
- Quantization accuracy change: +0.000533
- Notebook status: `APPROVED_FOR_PROTOTYPE`

## Hardware checks still required

This environment cannot flash the user's physical board. Before a formal showcase, compile against the installed ESP32/TensorFlowLite library versions, flash the exact target board, confirm tensor allocation and inference, verify the selected I2S pins, calibrate the microphone against a reference meter, and run quiet/speech/activity/clap tests. Do not describe this prototype as IEC-certified sound-level instrumentation.
