#pragma once

#include <Arduino.h>
#include <math.h>
#include "aurasync_tinycnn_weights.h"

struct AuraSyncPrediction {
  float stationaryBackground;
  float speechDominant;
  float nonSpeechActivity;
  float confidence;
  uint8_t classIndex;
};

class AuraSyncTinyCNN {
 public:
  static const char* className(uint8_t index) {
    switch (index) {
      case 0: return "stationary_background";
      case 1: return "speech_dominant";
      case 2: return "non_speech_activity";
      default: return "unknown";
    }
  }

  static AuraSyncPrediction predict(const float raw[AS_CNN_FRAMES][AS_CNN_CHANNELS]) {
    float input[AS_CNN_FRAMES][AS_CNN_CHANNELS];
    float layer1[AS_CNN_FRAMES][AS_CNN_FILTERS];
    float layer2[AS_CNN_FRAMES][AS_CNN_FILTERS];
    float pooled[AS_CNN_FILTERS * 2];
    float logits[AS_CNN_CLASSES];

    for (int t = 0; t < AS_CNN_FRAMES; ++t) {
      for (int c = 0; c < AS_CNN_CHANNELS; ++c) {
        const float mean = pgm_read_float(&AS_FEATURE_MEAN[c]);
        const float stddev = pgm_read_float(&AS_FEATURE_STD[c]);
        input[t][c] = (raw[t][c] - mean) / ((stddev > 1e-6f) ? stddev : 1.0f);
      }
    }

    for (int t = 0; t < AS_CNN_FRAMES; ++t) {
      for (int f = 0; f < AS_CNN_FILTERS; ++f) {
        float sum = pgm_read_float(&AS_B1[f]);
        for (int k = 0; k < AS_CNN_KERNEL; ++k) {
          const int sourceT = t + k - (AS_CNN_KERNEL / 2);
          if (sourceT < 0 || sourceT >= AS_CNN_FRAMES) continue;
          for (int c = 0; c < AS_CNN_CHANNELS; ++c) {
            const int weightIndex = ((f * AS_CNN_KERNEL + k) * AS_CNN_CHANNELS) + c;
            sum += input[sourceT][c] * pgm_read_float(&AS_W1[weightIndex]);
          }
        }
        layer1[t][f] = (sum > 0.0f) ? sum : 0.0f;
      }
    }

    for (int t = 0; t < AS_CNN_FRAMES; ++t) {
      for (int f = 0; f < AS_CNN_FILTERS; ++f) {
        float sum = pgm_read_float(&AS_B2[f]);
        for (int k = 0; k < AS_CNN_KERNEL; ++k) {
          const int sourceT = t + k - (AS_CNN_KERNEL / 2);
          if (sourceT < 0 || sourceT >= AS_CNN_FRAMES) continue;
          for (int c = 0; c < AS_CNN_FILTERS; ++c) {
            const int weightIndex = ((f * AS_CNN_KERNEL + k) * AS_CNN_FILTERS) + c;
            sum += layer1[sourceT][c] * pgm_read_float(&AS_W2[weightIndex]);
          }
        }
        layer2[t][f] = (sum > 0.0f) ? sum : 0.0f;
      }
    }

    for (int f = 0; f < AS_CNN_FILTERS; ++f) {
      float average = 0.0f;
      float maximum = layer2[0][f];
      for (int t = 0; t < AS_CNN_FRAMES; ++t) {
        average += layer2[t][f];
        if (layer2[t][f] > maximum) maximum = layer2[t][f];
      }
      pooled[f] = average / AS_CNN_FRAMES;
      pooled[AS_CNN_FILTERS + f] = maximum;
    }

    for (int c = 0; c < AS_CNN_CLASSES; ++c) {
      float sum = pgm_read_float(&AS_BD[c]);
      for (int i = 0; i < AS_CNN_FILTERS * 2; ++i) {
        sum += pooled[i] * pgm_read_float(&AS_WD[i * AS_CNN_CLASSES + c]);
      }
      logits[c] = sum;
    }

    float maxLogit = logits[0];
    for (int c = 1; c < AS_CNN_CLASSES; ++c) {
      if (logits[c] > maxLogit) maxLogit = logits[c];
    }
    float probability[AS_CNN_CLASSES];
    float probabilitySum = 0.0f;
    for (int c = 0; c < AS_CNN_CLASSES; ++c) {
      probability[c] = expf(logits[c] - maxLogit);
      probabilitySum += probability[c];
    }
    if (probabilitySum < 1e-9f) probabilitySum = 1.0f;
    for (int c = 0; c < AS_CNN_CLASSES; ++c) probability[c] /= probabilitySum;

    uint8_t best = 0;
    if (probability[1] > probability[best]) best = 1;
    if (probability[2] > probability[best]) best = 2;

    AuraSyncPrediction result;
    result.stationaryBackground = probability[0];
    result.speechDominant = probability[1];
    result.nonSpeechActivity = probability[2];
    result.confidence = probability[best];
    result.classIndex = best;
    return result;
  }
};

