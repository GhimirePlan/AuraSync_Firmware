# AuraSync TinyCNN Model Card

## Intended use

This model estimates the dominant one-second acoustic context for the AuraSync embedded showcase:

1. `stationary_background`
2. `speech_dominant`
3. `non_speech_activity`

It does not identify words, speakers or personal identities. It does not determine loudness or spikes.

## Architecture

- 16 time frames
- 17 level-invariant temporal/spectral features per frame
- Conv1D: 16 filters, kernel size 3, ReLU
- Conv1D: 16 filters, kernel size 3, ReLU
- Global average and maximum pooling
- Dense softmax output with 3 classes
- 1,632 learned parameters
- Float32 embedded inference; approximately 6.5 KB learned weights

## Features

Each one-second input produces:

- relative frame energy;
- zero-crossing rate;
- spectral centroid;
- spectral flatness;
- spectral flux;
- 12 normalized spectral-band energy ratios.

Absolute amplitude is intentionally excluded from the semantic classifier. Physical loudness remains in the calibrated DSP path.

## Dataset and split

- Dataset: AuraSync Verified Three-Class Dataset v2
- Accepted clips: 12,502
- Format: one second, 16 kHz, mono PCM16
- Training: 8,750
- Validation: 1,876
- Independent test: 1,876
- Split isolation: source/session-group controlled by the dataset build

## Independent test performance

| Class | Precision | Recall | F1 | Support |
|---|---:|---:|---:|---:|
| Stationary background | 0.549 | 0.639 | 0.590 | 415 |
| Speech dominant | 0.930 | 0.889 | 0.909 | 512 |
| Non-speech activity | 0.792 | 0.754 | 0.773 | 949 |
| Macro average | 0.757 | 0.761 | 0.757 | 1,876 |

Overall accuracy: **0.7655**.

Confusion matrix, using the class order above:

```text
265   6 144
 13 455  44
205  28 716
```

## Limitations

- Stationary-background and non-speech-activity scenes overlap acoustically; this is the weakest class boundary.
- Performance is not a substitute for human acoustic-comfort validation.
- Microphone/enclosure domain shift may reduce field performance.
- The model is a compact showcase model, not the final quantized publication model.
- Final research should report additional environment-level field testing and post-deployment calibration.

## Provenance

Weights were trained only on the verified AuraSync v2 training split. Validation was used for model selection, and the isolated test split was evaluated after selection.

