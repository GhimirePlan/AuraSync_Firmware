# AuraSync Three-Class INT8 Deployment Package

Repository: https://github.com/GhimirePlan/AS_Final_Dataset_.git
Commit: f68c273f7ffdf79bb8750ff54e6c7280790b3d01
Status: APPROVED_FOR_PROTOTYPE

CNN classes: stationary_background, speech_dominant, non_speech_activity

LOUD and SPIKE are DSP/state-machine outputs and are not CNN labels.

Float test accuracy: 0.8977
Float macro-F1: 0.8890
INT8 test accuracy: 0.8982
INT8 macro-F1: 0.8895
Quantization accuracy drop: -0.0005

The ESP32-S3 runtime must reproduce the exported FFT/Mel/CMN/normalization pipeline,
quantize with the tensor scale/zero point, invoke the INT8 DS-CNN, and fuse semantic
probabilities with the independent DSP loudness/spike/persistence state machine.
