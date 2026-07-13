/*
  AuraSync Showcase Firmware v2
  Target: ESP32-S3 N8R8 + INMP441

  Approved architecture:
    1. Fast DSP: estimated LAeq, peak, crest, adaptive baseline, speech-band ratio,
       spectral flux, microphone quality and DSP-only spike detection.
    2. TinyCNN: stationary_background, speech_dominant, non_speech_activity.
    3. Fusion: continuous Acoustic Disturbance Score (0-100).
    4. State machine: SMOOTH, PRE_WARNING, WARNING, LOUD, RECOVERY, SPIKE,
       MIC_ERROR.

  Privacy: raw audio is retained only in volatile RAM. There is no audio streaming,
  recording or download endpoint in this release.
*/

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include "driver/i2s_std.h"

#include "aurasync_config.h"
#include "aurasync_model.h"
#include "aurasync_dashboard.h"

// ----------------------------- Audio constants -----------------------------
constexpr int SAMPLE_RATE = 16000;
constexpr int DSP_BLOCK_MS = 100;
constexpr int DSP_BLOCK_SAMPLES = 1600;
constexpr int I2S_READ_SAMPLES = 400;
constexpr int CNN_WINDOW_SAMPLES = 16000;
constexpr int CNN_FRAME_SAMPLES = 1000;
constexpr int CNN_FFT_SIZE = 512;
constexpr int DSP_FFT_SIZE = 256;
constexpr float HIGH_PASS_ALPHA = 0.995f;

// ----------------------------- Runtime types -------------------------------
enum AuraState : uint8_t {
  STATE_MIC_ERROR,
  STATE_SMOOTH,
  STATE_PRE_WARNING,
  STATE_WARNING,
  STATE_LOUD,
  STATE_RECOVERY,
  STATE_SPIKE
};

struct DspResult {
  float laeq1sDba;
  float aWeightedDbfs;
  float peakDb;
  float rmsDb;
  float baselineDba;
  float snrDb;
  float crestDb;
  float speechBandRatio;
  float spectralFlux;
  float impulsiveness;
  float micQuality;
  float clipPercent;
  float zeroPercent;
};

struct TelemetrySnapshot {
  uint32_t sequence;
  uint32_t uptimeSeconds;
  AuraState state;
  float score;
  float persistence;
  DspResult dsp;
  AuraSyncPrediction cnn;
  float cnnInferenceMs;
  bool micOk;
  bool simulation;
  uint32_t i2sErrors;
  uint32_t eventCount;
  uint32_t spikeCount;
};

struct BiquadState {
  float z1;
  float z2;
};

// ----------------------------- Global services -----------------------------
WebServer server(80);
Preferences preferences;
i2s_chan_handle_t i2sRx = nullptr;
TaskHandle_t audioTaskHandle = nullptr;
portMUX_TYPE snapshotMux = portMUX_INITIALIZER_UNLOCKED;

TelemetrySnapshot snapshot = {};
volatile bool simulationMode = false;
bool microphoneReady = false;
float calibrationOffsetDb = AURASYNC_DEFAULT_CAL_OFFSET_DB;

// ------------------------------ Audio memory -------------------------------
int32_t i2sRaw[I2S_READ_SAMPLES];
int16_t audioBlock[DSP_BLOCK_SAMPLES];
int16_t cnnRing[CNN_WINDOW_SAMPLES];
int16_t cnnOrdered[CNN_WINDOW_SAMPLES];
int cnnWriteIndex = 0;
int cnnSamplesCollected = 0;

float dspFftReal[DSP_FFT_SIZE];
float dspFftImag[DSP_FFT_SIZE];
float dspPreviousSpectrum[(DSP_FFT_SIZE / 2) + 1] = {};
float cnnFftReal[CNN_FFT_SIZE];
float cnnFftImag[CNN_FFT_SIZE];

float weightedEnergyRing[10] = {};
float weightedEnergySum = 0.0f;
int weightedEnergyIndex = 0;
int weightedEnergyCount = 0;

float highPassLastX = 0.0f;
float highPassLastY = 0.0f;
BiquadState aWeightState[3] = {};

// A-weighting filter at 16 kHz, generated from the IEC analogue pole/zero model.
// Each row is b0, b1, b2, a1, a2 for a transposed direct-form-II biquad.
const float A_WEIGHT_SOS[3][5] = {
  {0.53148983f, 1.06297966f, 0.53148983f, 0.82156382f, 0.16874178f},
  {1.00000000f, -2.00000000f, 1.00000000f, -1.70550963f, 0.71598758f},
  {1.00000000f, -2.00000000f, 1.00000000f, -1.98388676f, 0.98395167f}
};

// -------------------------- Decision/state memory --------------------------
AuraState stableState = STATE_SMOOTH;
AuraState displayState = STATE_SMOOTH;
AuraSyncPrediction currentPrediction = {0.34f, 0.33f, 0.33f, 0.34f, 0};
float currentInferenceMs = 0.0f;
float adaptiveBaselineDba = 45.0f;
bool baselineInitialized = false;
float persistenceValue = 0.0f;
uint32_t spikeVisibleUntilMs = 0;
uint32_t eventCount = 0;
uint32_t spikeCount = 0;
uint32_t i2sErrorCount = 0;
int micBadMs = 0;
int loudMs = 0;
int warningMs = 0;
int preWarningMs = 0;
int recoveryMs = 0;

// ------------------------------- Utilities ---------------------------------
float clampFloat(float value, float low, float high) {
  if (value < low) return low;
  if (value > high) return high;
  return value;
}

int16_t clampInt16(int32_t value) {
  if (value > 32767) return 32767;
  if (value < -32768) return -32768;
  return static_cast<int16_t>(value);
}

float powerToDb(float power) {
  return 10.0f * log10f((power > 1e-12f) ? power : 1e-12f);
}

float amplitudeToDb(float amplitude) {
  return 20.0f * log10f((amplitude > 1e-9f) ? amplitude : 1e-9f);
}

const char* stateName(AuraState state) {
  switch (state) {
    case STATE_MIC_ERROR: return "MIC_ERROR";
    case STATE_SMOOTH: return "SMOOTH";
    case STATE_PRE_WARNING: return "PRE_WARNING";
    case STATE_WARNING: return "WARNING";
    case STATE_LOUD: return "LOUD";
    case STATE_RECOVERY: return "RECOVERY";
    case STATE_SPIKE: return "SPIKE";
    default: return "UNKNOWN";
  }
}

void fastFft(float* real, float* imag, int size) {
  int j = 0;
  for (int i = 1; i < size; ++i) {
    int bit = size >> 1;
    while (j & bit) {
      j ^= bit;
      bit >>= 1;
    }
    j ^= bit;
    if (i < j) {
      float temporary = real[i]; real[i] = real[j]; real[j] = temporary;
      temporary = imag[i]; imag[i] = imag[j]; imag[j] = temporary;
    }
  }

  for (int length = 2; length <= size; length <<= 1) {
    const float angleStep = -2.0f * PI / length;
    for (int start = 0; start < size; start += length) {
      for (int offset = 0; offset < length / 2; ++offset) {
        const float angle = angleStep * offset;
        const float cosine = cosf(angle);
        const float sine = sinf(angle);
        const int evenIndex = start + offset;
        const int oddIndex = evenIndex + length / 2;
        const float oddReal = real[oddIndex] * cosine - imag[oddIndex] * sine;
        const float oddImag = real[oddIndex] * sine + imag[oddIndex] * cosine;
        real[oddIndex] = real[evenIndex] - oddReal;
        imag[oddIndex] = imag[evenIndex] - oddImag;
        real[evenIndex] += oddReal;
        imag[evenIndex] += oddImag;
      }
    }
  }
}

float applyAWeighting(float input) {
  float value = input;
  for (int stage = 0; stage < 3; ++stage) {
    const float output = A_WEIGHT_SOS[stage][0] * value + aWeightState[stage].z1;
    aWeightState[stage].z1 = A_WEIGHT_SOS[stage][1] * value
                            - A_WEIGHT_SOS[stage][3] * output
                            + aWeightState[stage].z2;
    aWeightState[stage].z2 = A_WEIGHT_SOS[stage][2] * value
                            - A_WEIGHT_SOS[stage][4] * output;
    value = output;
  }
  return value;
}

void resetSignalMemory() {
  highPassLastX = 0.0f;
  highPassLastY = 0.0f;
  memset(aWeightState, 0, sizeof(aWeightState));
  memset(weightedEnergyRing, 0, sizeof(weightedEnergyRing));
  memset(dspPreviousSpectrum, 0, sizeof(dspPreviousSpectrum));
  weightedEnergySum = 0.0f;
  weightedEnergyIndex = 0;
  weightedEnergyCount = 0;
  cnnWriteIndex = 0;
  cnnSamplesCollected = 0;
}

void resetDecisionMemory() {
  stableState = STATE_SMOOTH;
  displayState = STATE_SMOOTH;
  persistenceValue = 0.0f;
  micBadMs = loudMs = warningMs = preWarningMs = recoveryMs = 0;
  spikeVisibleUntilMs = 0;
}

// ----------------------------- I2S microphone ------------------------------
bool configureMicrophone() {
  i2s_chan_config_t channelConfig = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  channelConfig.dma_desc_num = 12;
  channelConfig.dma_frame_num = I2S_READ_SAMPLES;
  channelConfig.auto_clear_after_cb = true;

  esp_err_t result = i2s_new_channel(&channelConfig, nullptr, &i2sRx);
  if (result != ESP_OK) {
    Serial.printf("I2S channel creation failed: %d\n", static_cast<int>(result));
    return false;
  }

  i2s_std_config_t standardConfig = {};
  standardConfig.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE);
  standardConfig.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
    I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO
  );
#if AURASYNC_MIC_USE_LEFT_CHANNEL
  standardConfig.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
#else
  standardConfig.slot_cfg.slot_mask = I2S_STD_SLOT_RIGHT;
#endif
  standardConfig.gpio_cfg.mclk = I2S_GPIO_UNUSED;
  standardConfig.gpio_cfg.bclk = static_cast<gpio_num_t>(AURASYNC_I2S_BCLK_PIN);
  standardConfig.gpio_cfg.ws = static_cast<gpio_num_t>(AURASYNC_I2S_WS_PIN);
  standardConfig.gpio_cfg.dout = I2S_GPIO_UNUSED;
  standardConfig.gpio_cfg.din = static_cast<gpio_num_t>(AURASYNC_I2S_DATA_PIN);
  standardConfig.gpio_cfg.invert_flags.mclk_inv = false;
  standardConfig.gpio_cfg.invert_flags.bclk_inv = false;
  standardConfig.gpio_cfg.invert_flags.ws_inv = false;

  result = i2s_channel_init_std_mode(i2sRx, &standardConfig);
  if (result != ESP_OK) {
    Serial.printf("I2S standard-mode initialization failed: %d\n", static_cast<int>(result));
    i2s_del_channel(i2sRx);
    i2sRx = nullptr;
    return false;
  }
  result = i2s_channel_enable(i2sRx);
  if (result != ESP_OK) {
    Serial.printf("I2S enable failed: %d\n", static_cast<int>(result));
    i2s_del_channel(i2sRx);
    i2sRx = nullptr;
    return false;
  }
  Serial.println("INMP441 I2S microphone initialized at 16 kHz mono.");
  return true;
}

bool readAudioBlock() {
  if (!microphoneReady || i2sRx == nullptr) return false;
  int filled = 0;
  while (filled < DSP_BLOCK_SAMPLES) {
    size_t bytesRead = 0;
    const esp_err_t result = i2s_channel_read(
      i2sRx, i2sRaw, sizeof(i2sRaw), &bytesRead, pdMS_TO_TICKS(300)
    );
    if (result != ESP_OK || bytesRead == 0) {
      ++i2sErrorCount;
      return false;
    }
    const int samplesRead = bytesRead / sizeof(int32_t);
    for (int i = 0; i < samplesRead && filled < DSP_BLOCK_SAMPLES; ++i) {
      int32_t converted = i2sRaw[i] >> AURASYNC_MIC_SHIFT_BITS;
      converted = static_cast<int32_t>(converted * AURASYNC_MIC_DIGITAL_GAIN);
      const float input = static_cast<float>(clampInt16(converted));
      const float cleaned = input - highPassLastX + HIGH_PASS_ALPHA * highPassLastY;
      highPassLastX = input;
      highPassLastY = cleaned;
      const int16_t sample = clampInt16(static_cast<int32_t>(cleaned));
      audioBlock[filled++] = sample;
      cnnRing[cnnWriteIndex] = sample;
      cnnWriteIndex = (cnnWriteIndex + 1) % CNN_WINDOW_SAMPLES;
      if (cnnSamplesCollected < CNN_WINDOW_SAMPLES) ++cnnSamplesCollected;
    }
  }
  return true;
}

// ------------------------------ Fast DSP -----------------------------------
DspResult computeDsp() {
  DspResult result = {};
  double unweightedEnergy = 0.0;
  double aWeightedEnergy = 0.0;
  int32_t peakAbsolute = 1;
  int clipping = 0;
  int zeros = 0;

  for (int i = 0; i < DSP_BLOCK_SAMPLES; ++i) {
    const int32_t sample = audioBlock[i];
    const int32_t absolute = abs(sample);
    if (absolute > peakAbsolute) peakAbsolute = absolute;
    if (absolute > 32000) ++clipping;
    if (absolute < 4) ++zeros;
    const float normalized = sample / 32768.0f;
    unweightedEnergy += normalized * normalized;
    const float weighted = applyAWeighting(normalized);
    aWeightedEnergy += weighted * weighted;
  }

  const float blockUnweightedPower = static_cast<float>(unweightedEnergy / DSP_BLOCK_SAMPLES);
  const float blockWeightedPower = static_cast<float>(aWeightedEnergy / DSP_BLOCK_SAMPLES);
  weightedEnergySum -= weightedEnergyRing[weightedEnergyIndex];
  weightedEnergyRing[weightedEnergyIndex] = blockWeightedPower;
  weightedEnergySum += blockWeightedPower;
  weightedEnergyIndex = (weightedEnergyIndex + 1) % 10;
  if (weightedEnergyCount < 10) ++weightedEnergyCount;

  float localCalibration;
  portENTER_CRITICAL(&snapshotMux);
  localCalibration = calibrationOffsetDb;
  portEXIT_CRITICAL(&snapshotMux);

  const float oneSecondPower = weightedEnergySum / max(1, weightedEnergyCount);
  result.aWeightedDbfs = powerToDb(oneSecondPower);
  result.laeq1sDba = result.aWeightedDbfs + localCalibration;
  result.rmsDb = powerToDb(blockUnweightedPower) + localCalibration;
  result.peakDb = amplitudeToDb(peakAbsolute / 32768.0f) + localCalibration;
  result.crestDb = result.peakDb - result.rmsDb;
  result.clipPercent = 100.0f * clipping / DSP_BLOCK_SAMPLES;
  result.zeroPercent = 100.0f * zeros / DSP_BLOCK_SAMPLES;

  // 256-point fast spectral analysis for speech-band energy and spectral flux.
  for (int i = 0; i < DSP_FFT_SIZE; ++i) {
    const float window = 0.5f - 0.5f * cosf(2.0f * PI * i / (DSP_FFT_SIZE - 1));
    dspFftReal[i] = (audioBlock[i] / 32768.0f) * window;
    dspFftImag[i] = 0.0f;
  }
  fastFft(dspFftReal, dspFftImag, DSP_FFT_SIZE);
  float totalPower = 1e-12f;
  float speechPower = 0.0f;
  float normalizedSpectrum[(DSP_FFT_SIZE / 2) + 1];
  for (int k = 0; k <= DSP_FFT_SIZE / 2; ++k) {
    const float power = dspFftReal[k] * dspFftReal[k] + dspFftImag[k] * dspFftImag[k];
    normalizedSpectrum[k] = power;
    totalPower += power;
    const float frequency = k * (static_cast<float>(SAMPLE_RATE) / DSP_FFT_SIZE);
    if (frequency >= 300.0f && frequency < 3400.0f) speechPower += power;
  }
  result.speechBandRatio = clampFloat(speechPower / totalPower, 0.0f, 1.0f);
  float fluxSquared = 0.0f;
  for (int k = 0; k <= DSP_FFT_SIZE / 2; ++k) {
    const float normalized = normalizedSpectrum[k] / totalPower;
    const float difference = normalized - dspPreviousSpectrum[k];
    fluxSquared += difference * difference;
    dspPreviousSpectrum[k] = normalized;
  }
  result.spectralFlux = sqrtf(fluxSquared);

  result.micQuality = 100.0f;
  if (result.zeroPercent > 80.0f) result.micQuality -= 65.0f;
  if (result.clipPercent > 2.0f) result.micQuality -= 35.0f;
  if (result.aWeightedDbfs < -80.0f) result.micQuality -= 45.0f;
  result.micQuality = clampFloat(result.micQuality, 0.0f, 100.0f);

  if (!baselineInitialized) {
    adaptiveBaselineDba = result.laeq1sDba;
    baselineInitialized = true;
  } else if (result.laeq1sDba < adaptiveBaselineDba + 6.0f && result.crestDb < 16.0f) {
    adaptiveBaselineDba = 0.998f * adaptiveBaselineDba + 0.002f * result.laeq1sDba;
  }
  result.baselineDba = adaptiveBaselineDba;
  result.snrDb = max(0.0f, result.laeq1sDba - adaptiveBaselineDba);

  const float crestComponent = clampFloat((result.crestDb - 6.0f) / 16.0f, 0.0f, 1.0f);
  const float fluxComponent = clampFloat(result.spectralFlux / 0.35f, 0.0f, 1.0f);
  const float peakComponent = clampFloat((result.peakDb - adaptiveBaselineDba - 10.0f) / 25.0f, 0.0f, 1.0f);
  result.impulsiveness = clampFloat(
    0.40f * crestComponent + 0.35f * fluxComponent + 0.25f * peakComponent,
    0.0f, 1.0f
  );
  return result;
}

// --------------------------- TinyCNN features ------------------------------
float bandRatio(const float* power, float totalPower, float low, float high) {
  float sum = 0.0f;
  for (int k = 0; k <= CNN_FFT_SIZE / 2; ++k) {
    const float frequency = k * (static_cast<float>(SAMPLE_RATE) / CNN_FFT_SIZE);
    if (frequency >= low && frequency < high) sum += power[k];
  }
  return sum / ((totalPower > 1e-12f) ? totalPower : 1e-12f);
}

void extractCnnFeatures(const int16_t* audio, float features[AS_CNN_FRAMES][AS_CNN_CHANNELS]) {
  float frameLogRms[AS_CNN_FRAMES];
  float previousSpectrum[(CNN_FFT_SIZE / 2) + 1] = {};
  const float bandEdges[13] = {
    0.0f, 125.0f, 250.0f, 500.0f, 750.0f, 1000.0f, 1500.0f,
    2000.0f, 3000.0f, 4000.0f, 5000.0f, 6500.0f, 8001.0f
  };
  float meanLogRms = 0.0f;

  for (int frame = 0; frame < AS_CNN_FRAMES; ++frame) {
    const int start = frame * CNN_FRAME_SAMPLES;
    double energy = 0.0;
    int zeroCrossings = 0;
    for (int i = 0; i < CNN_FRAME_SAMPLES; ++i) {
      const float sample = audio[start + i] / 32768.0f;
      energy += sample * sample;
      if (i > 0) {
        const bool currentSign = audio[start + i] >= 0;
        const bool previousSign = audio[start + i - 1] >= 0;
        if (currentSign != previousSign) ++zeroCrossings;
      }
    }
    const float rms = sqrtf(static_cast<float>(energy / CNN_FRAME_SAMPLES) + 1e-12f);
    frameLogRms[frame] = 20.0f * log10f(rms + 1e-9f);
    meanLogRms += frameLogRms[frame];
    features[frame][1] = static_cast<float>(zeroCrossings) / (CNN_FRAME_SAMPLES - 1);

    for (int i = 0; i < CNN_FFT_SIZE; ++i) {
      const float window = 0.5f - 0.5f * cosf(2.0f * PI * i / (CNN_FFT_SIZE - 1));
      cnnFftReal[i] = (audio[start + i] / 32768.0f) * window;
      cnnFftImag[i] = 0.0f;
    }
    fastFft(cnnFftReal, cnnFftImag, CNN_FFT_SIZE);

    float spectrum[(CNN_FFT_SIZE / 2) + 1];
    float totalPower = 1e-12f;
    double logPowerSum = 0.0;
    for (int k = 0; k <= CNN_FFT_SIZE / 2; ++k) {
      const float power = cnnFftReal[k] * cnnFftReal[k] + cnnFftImag[k] * cnnFftImag[k];
      spectrum[k] = power;
      totalPower += power;
      logPowerSum += logf(power + 1e-12f);
    }

    float centroid = 0.0f;
    float fluxSquared = 0.0f;
    for (int k = 0; k <= CNN_FFT_SIZE / 2; ++k) {
      const float normalized = spectrum[k] / totalPower;
      const float frequencyNormalized = (k * (static_cast<float>(SAMPLE_RATE) / CNN_FFT_SIZE)) / 8000.0f;
      centroid += normalized * frequencyNormalized;
      const float difference = normalized - previousSpectrum[k];
      if (frame > 0) fluxSquared += difference * difference;
      previousSpectrum[k] = normalized;
    }
    const float geometricMean = expf(static_cast<float>(logPowerSum / ((CNN_FFT_SIZE / 2) + 1)));
    const float arithmeticMean = totalPower / ((CNN_FFT_SIZE / 2) + 1);
    features[frame][2] = clampFloat(centroid, 0.0f, 1.0f);
    features[frame][3] = clampFloat(geometricMean / (arithmeticMean + 1e-12f), 0.0f, 1.0f);
    features[frame][4] = (frame == 0) ? 0.0f : sqrtf(fluxSquared);
    for (int band = 0; band < 12; ++band) {
      features[frame][5 + band] = bandRatio(
        spectrum, totalPower, bandEdges[band], bandEdges[band + 1]
      );
    }
  }

  meanLogRms /= AS_CNN_FRAMES;
  for (int frame = 0; frame < AS_CNN_FRAMES; ++frame) {
    features[frame][0] = clampFloat((frameLogRms[frame] - meanLogRms) / 20.0f, -1.0f, 1.0f);
  }
}

void runTinyCnn() {
  if (cnnSamplesCollected < CNN_WINDOW_SAMPLES) return;
  for (int i = 0; i < CNN_WINDOW_SAMPLES; ++i) {
    int source = cnnWriteIndex + i;
    if (source >= CNN_WINDOW_SAMPLES) source -= CNN_WINDOW_SAMPLES;
    cnnOrdered[i] = cnnRing[source];
  }

  float features[AS_CNN_FRAMES][AS_CNN_CHANNELS];
  const uint32_t started = micros();
  extractCnnFeatures(cnnOrdered, features);
  AuraSyncPrediction newPrediction = AuraSyncTinyCNN::predict(features);
  currentInferenceMs = (micros() - started) / 1000.0f;

  // Probability smoothing reduces rapid semantic flicker without affecting DSP spikes.
  const float alpha = 0.40f;
  currentPrediction.stationaryBackground =
    (1.0f - alpha) * currentPrediction.stationaryBackground + alpha * newPrediction.stationaryBackground;
  currentPrediction.speechDominant =
    (1.0f - alpha) * currentPrediction.speechDominant + alpha * newPrediction.speechDominant;
  currentPrediction.nonSpeechActivity =
    (1.0f - alpha) * currentPrediction.nonSpeechActivity + alpha * newPrediction.nonSpeechActivity;

  currentPrediction.classIndex = 0;
  currentPrediction.confidence = currentPrediction.stationaryBackground;
  if (currentPrediction.speechDominant > currentPrediction.confidence) {
    currentPrediction.classIndex = 1;
    currentPrediction.confidence = currentPrediction.speechDominant;
  }
  if (currentPrediction.nonSpeechActivity > currentPrediction.confidence) {
    currentPrediction.classIndex = 2;
    currentPrediction.confidence = currentPrediction.nonSpeechActivity;
  }
}

// -------------------------- Fusion + state machine -------------------------
void publishSnapshot(const DspResult& dsp, float score, bool micOk, bool simulation) {
  TelemetrySnapshot updated;
  portENTER_CRITICAL(&snapshotMux);
  updated = snapshot;
  portEXIT_CRITICAL(&snapshotMux);

  ++updated.sequence;
  updated.uptimeSeconds = millis() / 1000;
  updated.state = displayState;
  updated.score = score;
  updated.persistence = persistenceValue;
  updated.dsp = dsp;
  updated.cnn = currentPrediction;
  updated.cnnInferenceMs = currentInferenceMs;
  updated.micOk = micOk;
  updated.simulation = simulation;
  updated.i2sErrors = i2sErrorCount;
  updated.eventCount = eventCount;
  updated.spikeCount = spikeCount;

  portENTER_CRITICAL(&snapshotMux);
  snapshot = updated;
  portEXIT_CRITICAL(&snapshotMux);
}

void updateFusionAndState(const DspResult& dsp, bool micOk, bool simulation) {
  const float loudness = clampFloat((dsp.laeq1sDba - 50.0f) / 30.0f, 0.0f, 1.0f);
  const float speech = clampFloat(currentPrediction.speechDominant, 0.0f, 1.0f);
  const float instantDisturbance = max(loudness, speech);
  persistenceValue = 0.94f * persistenceValue + 0.06f * instantDisturbance;

  const float score = 100.0f * clampFloat(
    AURASYNC_WEIGHT_LOUDNESS * loudness
    + AURASYNC_WEIGHT_SPEECH * speech
    + AURASYNC_WEIGHT_PERSISTENCE * persistenceValue
    + AURASYNC_WEIGHT_IMPULSIVENESS * dsp.impulsiveness,
    0.0f, 1.0f
  );

  if (!micOk) micBadMs += DSP_BLOCK_MS;
  else micBadMs = max(0, micBadMs - DSP_BLOCK_MS);

  const bool spikeCandidate =
    dsp.peakDb >= AURASYNC_SPIKE_MIN_PEAK_DB
    && dsp.crestDb >= AURASYNC_SPIKE_MIN_CREST_DB
    && (dsp.peakDb - dsp.baselineDba) >= AURASYNC_SPIKE_ABOVE_BASELINE_DB;
  if (spikeCandidate && millis() > spikeVisibleUntilMs) {
    spikeVisibleUntilMs = millis() + 800;
    ++spikeCount;
    ++eventCount;
  }

  const bool loudCandidate = (stableState == STATE_LOUD)
    ? dsp.laeq1sDba >= AURASYNC_LOUD_EXIT_DBA
    : dsp.laeq1sDba >= AURASYNC_LOUD_ENTER_DBA;
  if (loudCandidate) loudMs += DSP_BLOCK_MS;
  else loudMs = max(0, loudMs - DSP_BLOCK_MS);

  if (score >= AURASYNC_WARNING_SCORE) warningMs += DSP_BLOCK_MS;
  else warningMs = max(0, warningMs - DSP_BLOCK_MS);
  if (score >= AURASYNC_PREWARNING_SCORE) preWarningMs += DSP_BLOCK_MS;
  else preWarningMs = max(0, preWarningMs - DSP_BLOCK_MS);

  const bool recovering = score < 35.0f && !loudCandidate;
  if (recovering) recoveryMs += DSP_BLOCK_MS;
  else recoveryMs = 0;

  const AuraState oldState = stableState;
  if (micBadMs >= 2000) {
    stableState = STATE_MIC_ERROR;
  } else if (loudMs >= 2500) {
    stableState = STATE_LOUD;
  } else if (warningMs >= 3000) {
    stableState = STATE_WARNING;
  } else if (preWarningMs >= 1500) {
    stableState = STATE_PRE_WARNING;
  } else if (recovering &&
             (stableState == STATE_LOUD || stableState == STATE_WARNING ||
              stableState == STATE_PRE_WARNING || stableState == STATE_MIC_ERROR)) {
    stableState = STATE_RECOVERY;
  }
  if (stableState == STATE_RECOVERY && recoveryMs >= 6000) stableState = STATE_SMOOTH;
  if (stableState == STATE_MIC_ERROR && micOk && recoveryMs >= 2000) stableState = STATE_RECOVERY;

  if (stableState != oldState) {
    ++eventCount;
    Serial.printf("State: %s -> %s | score %.1f | LAeq %.1f dBA\n",
                  stateName(oldState), stateName(stableState), score, dsp.laeq1sDba);
  }
  displayState = (millis() < spikeVisibleUntilMs && stableState != STATE_MIC_ERROR)
    ? STATE_SPIKE : stableState;
  publishSnapshot(dsp, score, micOk, simulation);
}

// -------------------------- Labelled demonstration -------------------------
void runSimulationStep() {
  const float seconds = (millis() % 60000UL) / 1000.0f;
  DspResult dsp = {};
  dsp.micQuality = 100.0f;
  dsp.zeroPercent = 0.2f;
  dsp.clipPercent = 0.0f;
  currentInferenceMs = 0.42f;

  if (seconds < 12.0f) {
    dsp.laeq1sDba = 48.0f + 1.2f * sinf(seconds);
    dsp.peakDb = dsp.laeq1sDba + 7.0f;
    dsp.speechBandRatio = 0.28f;
    dsp.spectralFlux = 0.025f;
    currentPrediction = {0.86f, 0.05f, 0.09f, 0.86f, 0};
  } else if (seconds < 22.0f) {
    dsp.laeq1sDba = 65.0f + 1.5f * sinf(seconds * 1.3f);
    dsp.peakDb = dsp.laeq1sDba + 10.0f;
    dsp.speechBandRatio = 0.66f;
    dsp.spectralFlux = 0.08f;
    currentPrediction = {0.07f, 0.86f, 0.07f, 0.86f, 1};
  } else if (seconds < 37.0f) {
    dsp.laeq1sDba = 72.0f + 1.0f * sinf(seconds * 1.7f);
    dsp.peakDb = dsp.laeq1sDba + 11.0f;
    dsp.speechBandRatio = 0.72f;
    dsp.spectralFlux = 0.12f;
    currentPrediction = {0.05f, 0.90f, 0.05f, 0.90f, 1};
  } else if (seconds < 44.0f) {
    dsp.laeq1sDba = 80.0f + 1.0f * sinf(seconds);
    dsp.peakDb = 88.0f;
    dsp.speechBandRatio = 0.43f;
    dsp.spectralFlux = 0.18f;
    currentPrediction = {0.03f, 0.25f, 0.72f, 0.72f, 2};
  } else if (seconds < 45.0f) {
    dsp.laeq1sDba = 74.0f;
    dsp.peakDb = 98.0f;
    dsp.speechBandRatio = 0.31f;
    dsp.spectralFlux = 0.62f;
    currentPrediction = {0.02f, 0.08f, 0.90f, 0.90f, 2};
  } else if (seconds < 54.0f) {
    dsp.laeq1sDba = 58.0f - (seconds - 45.0f) * 0.7f;
    dsp.peakDb = dsp.laeq1sDba + 8.0f;
    dsp.speechBandRatio = 0.35f;
    dsp.spectralFlux = 0.045f;
    currentPrediction = {0.70f, 0.12f, 0.18f, 0.70f, 0};
  } else {
    dsp.laeq1sDba = 48.0f;
    dsp.peakDb = 55.0f;
    dsp.speechBandRatio = 0.25f;
    dsp.spectralFlux = 0.02f;
    currentPrediction = {0.88f, 0.04f, 0.08f, 0.88f, 0};
  }
  dsp.aWeightedDbfs = dsp.laeq1sDba - calibrationOffsetDb;
  dsp.rmsDb = dsp.laeq1sDba;
  dsp.baselineDba = 48.0f;
  dsp.snrDb = max(0.0f, dsp.laeq1sDba - dsp.baselineDba);
  dsp.crestDb = dsp.peakDb - dsp.rmsDb;
  dsp.impulsiveness = clampFloat(
    0.45f * clampFloat((dsp.crestDb - 6.0f) / 16.0f, 0.0f, 1.0f)
    + 0.55f * clampFloat(dsp.spectralFlux / 0.35f, 0.0f, 1.0f), 0.0f, 1.0f
  );
  updateFusionAndState(dsp, true, true);
}

// ------------------------------- Audio task --------------------------------
void audioTask(void* parameter) {
  bool previousSimulation = !simulationMode;
  uint8_t cnnBlockCounter = 0;
  while (true) {
    const bool simulate = simulationMode;
    if (simulate != previousSimulation) {
      resetSignalMemory();
      resetDecisionMemory();
      previousSimulation = simulate;
      Serial.printf("Data source changed to: %s\n", simulate ? "labelled simulation" : "live microphone");
    }

    if (simulate) {
      runSimulationStep();
      vTaskDelay(pdMS_TO_TICKS(DSP_BLOCK_MS));
      continue;
    }

    if (!microphoneReady || !readAudioBlock()) {
      DspResult failed = {};
      failed.laeq1sDba = 0.0f;
      failed.baselineDba = adaptiveBaselineDba;
      failed.micQuality = 0.0f;
      updateFusionAndState(failed, false, false);
      vTaskDelay(pdMS_TO_TICKS(DSP_BLOCK_MS));
      continue;
    }

    DspResult dsp = computeDsp();
    ++cnnBlockCounter;
    if (cnnBlockCounter >= 10) {
      cnnBlockCounter = 0;
      runTinyCnn();
    }
    const bool micOk = dsp.micQuality >= 35.0f;
    updateFusionAndState(dsp, micOk, false);
  }
}

// ------------------------------ Web dashboard ------------------------------
TelemetrySnapshot copySnapshot() {
  TelemetrySnapshot copy;
  portENTER_CRITICAL(&snapshotMux);
  copy = snapshot;
  portEXIT_CRITICAL(&snapshotMux);
  return copy;
}

void sendJson(int status, const String& body) {
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  server.send(status, "application/json", body);
}

void handleDashboard() {
  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "text/html", AURASYNC_DASHBOARD_HTML);
}

void handleStatus() {
  const TelemetrySnapshot data = copySnapshot();
  const char* networkMode = (WiFi.status() == WL_CONNECTED) ? "AP + Wi-Fi" : "Local AP";
  const int rssi = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0;
  char json[2400];
  snprintf(json, sizeof(json),
    "{"
      "\"device_id\":\"%s\","
      "\"firmware_version\":\"%s\","
      "\"model_version\":\"%s\","
      "\"sequence\":%lu,"
      "\"uptime_s\":%lu,"
      "\"data_source\":\"%s\","
      "\"state\":\"%s\","
      "\"score\":%.2f,"
      "\"persistence\":%.3f,"
      "\"dsp\":{" 
        "\"laeq_1s_dba\":%.2f,"
        "\"peak_db\":%.2f,"
        "\"baseline_dba\":%.2f,"
        "\"snr_db\":%.2f,"
        "\"crest_db\":%.2f,"
        "\"speech_band_ratio\":%.4f,"
        "\"spectral_flux\":%.4f,"
        "\"impulsiveness\":%.4f,"
        "\"a_weighted_dbfs\":%.2f"
      "},"
      "\"cnn\":{" 
        "\"stationary_background\":%.4f,"
        "\"speech_dominant\":%.4f,"
        "\"non_speech_activity\":%.4f,"
        "\"predicted\":\"%s\","
        "\"confidence\":%.4f,"
        "\"inference_ms\":%.3f"
      "},"
      "\"health\":{" 
        "\"mic_ok\":%s,"
        "\"mic_quality\":%.1f,"
        "\"clip_pct\":%.3f,"
        "\"zero_pct\":%.3f,"
        "\"i2s_errors\":%lu,"
        "\"free_heap\":%lu"
      "},"
      "\"counters\":{" 
        "\"events\":%lu,"
        "\"spikes\":%lu"
      "},"
      "\"network\":{" 
        "\"mode\":\"%s\","
        "\"rssi\":%d,"
        "\"ap_ip\":\"192.168.4.1\""
      "}"
    "}",
    AURASYNC_DEVICE_ID,
    AURASYNC_FIRMWARE_VERSION,
    AURASYNC_MODEL_VERSION,
    static_cast<unsigned long>(data.sequence),
    static_cast<unsigned long>(data.uptimeSeconds),
    data.simulation ? "simulation" : "microphone",
    stateName(data.state),
    data.score,
    data.persistence,
    data.dsp.laeq1sDba,
    data.dsp.peakDb,
    data.dsp.baselineDba,
    data.dsp.snrDb,
    data.dsp.crestDb,
    data.dsp.speechBandRatio,
    data.dsp.spectralFlux,
    data.dsp.impulsiveness,
    data.dsp.aWeightedDbfs,
    data.cnn.stationaryBackground,
    data.cnn.speechDominant,
    data.cnn.nonSpeechActivity,
    AuraSyncTinyCNN::className(data.cnn.classIndex),
    data.cnn.confidence,
    data.cnnInferenceMs,
    data.micOk ? "true" : "false",
    data.dsp.micQuality,
    data.dsp.clipPercent,
    data.dsp.zeroPercent,
    static_cast<unsigned long>(data.i2sErrors),
    static_cast<unsigned long>(ESP.getFreeHeap()),
    static_cast<unsigned long>(data.eventCount),
    static_cast<unsigned long>(data.spikeCount),
    networkMode,
    rssi
  );
  sendJson(200, json);
}

void handleDemoToggle() {
#if AURASYNC_ALLOW_SIMULATION
  simulationMode = !simulationMode;
  const String message = simulationMode
    ? "{\"ok\":true,\"message\":\"Labelled showcase simulation enabled.\"}"
    : "{\"ok\":true,\"message\":\"Live microphone processing enabled.\"}";
  sendJson(200, message);
#else
  sendJson(403, "{\"ok\":false,\"message\":\"Simulation is disabled in this build.\"}");
#endif
}

void handleCalibration() {
  if (simulationMode) {
    sendJson(409, "{\"ok\":false,\"message\":\"Disable simulation before calibration.\"}");
    return;
  }
  if (!server.hasArg("db")) {
    sendJson(400, "{\"ok\":false,\"message\":\"Reference dBA value is required.\"}");
    return;
  }
  const float knownDb = server.arg("db").toFloat();
  if (knownDb < 30.0f || knownDb > 120.0f) {
    sendJson(400, "{\"ok\":false,\"message\":\"Reference value must be 30–120 dBA.\"}");
    return;
  }
  const TelemetrySnapshot data = copySnapshot();
  if (!data.micOk || data.dsp.aWeightedDbfs < -100.0f) {
    sendJson(409, "{\"ok\":false,\"message\":\"A stable microphone signal is required.\"}");
    return;
  }
  const float newOffset = knownDb - data.dsp.aWeightedDbfs;
  portENTER_CRITICAL(&snapshotMux);
  calibrationOffsetDb = newOffset;
  portEXIT_CRITICAL(&snapshotMux);
  preferences.putFloat("calOffset", newOffset);
  char response[220];
  snprintf(response, sizeof(response),
           "{\"ok\":true,\"message\":\"Calibration stored. New offset %.2f dB.\",\"cal_offset\":%.3f}",
           newOffset, newOffset);
  sendJson(200, response);
}

void configureWebServer() {
  server.on("/", HTTP_GET, handleDashboard);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/demo/toggle", HTTP_POST, handleDemoToggle);
  server.on("/api/calibrate", HTTP_POST, handleCalibration);
  server.on("/api/health", HTTP_GET, []() {
    sendJson(200, "{\"ok\":true,\"service\":\"aurasync-edge-dashboard\"}");
  });
  server.onNotFound([]() {
    sendJson(404, "{\"ok\":false,\"message\":\"Endpoint not found.\"}");
  });
  server.begin();
}

// ----------------------------- Network setup -------------------------------
void configureNetwork() {
  WiFi.mode(WIFI_AP_STA);
  const bool apStarted = WiFi.softAP(AURASYNC_AP_SSID, AURASYNC_AP_PASSWORD);
  Serial.printf("Showcase access point: %s (%s)\n", AURASYNC_AP_SSID, apStarted ? "started" : "failed");
  Serial.print("Local dashboard: http://");
  Serial.println(WiFi.softAPIP());

  if (strlen(AURASYNC_WIFI_SSID) > 0) {
    WiFi.begin(AURASYNC_WIFI_SSID, AURASYNC_WIFI_PASSWORD);
    Serial.printf("Connecting to Wi-Fi: %s", AURASYNC_WIFI_SSID);
    const uint32_t deadline = millis() + 12000;
    while (WiFi.status() != WL_CONNECTED && millis() < deadline) {
      delay(250);
      Serial.print('.');
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("Network dashboard: http://");
      Serial.println(WiFi.localIP());
    } else {
      Serial.println("Wi-Fi not connected; local access-point dashboard remains available.");
    }
  }

  if (MDNS.begin("aurasync")) {
    MDNS.addService("http", "tcp", 80);
    Serial.println("mDNS dashboard: http://aurasync.local");
  }
}

// ------------------------------ Arduino entry ------------------------------
void setup() {
  Serial.begin(115200);
  delay(700);
  Serial.println();
  Serial.println("====================================================");
  Serial.printf("AuraSync %s | model %s\n", AURASYNC_FIRMWARE_VERSION, AURASYNC_MODEL_VERSION);
  Serial.println("DSP + three-class TinyCNN + fusion + state machine");
  Serial.println("====================================================");

  preferences.begin("aurasync", false);
  calibrationOffsetDb = preferences.getFloat("calOffset", AURASYNC_DEFAULT_CAL_OFFSET_DB);
  Serial.printf("Calibration offset: %.2f dB\n", calibrationOffsetDb);

  resetSignalMemory();
  resetDecisionMemory();
  microphoneReady = configureMicrophone();
#if AURASYNC_AUTO_SIMULATE_IF_MIC_FAILS
  simulationMode = !microphoneReady;
#else
  simulationMode = false;
#endif
  if (!microphoneReady) {
    Serial.printf("Microphone unavailable. %s\n",
                  simulationMode ? "Starting visibly labelled simulation." : "MIC_ERROR will be reported.");
  }

  configureNetwork();
  configureWebServer();

  snapshot.cnn = currentPrediction;
  snapshot.state = microphoneReady ? STATE_SMOOTH : STATE_MIC_ERROR;
  snapshot.micOk = microphoneReady;
  snapshot.simulation = simulationMode;

  const BaseType_t taskCreated = xTaskCreatePinnedToCore(
    audioTask,
    "AuraSyncAudio",
    24576,
    nullptr,
    3,
    &audioTaskHandle,
    1
  );
  if (taskCreated != pdPASS) {
    Serial.println("CRITICAL: Audio task creation failed.");
    snapshot.state = STATE_MIC_ERROR;
    snapshot.micOk = false;
  }
}

void loop() {
  server.handleClient();

  static uint32_t lastReconnectAttempt = 0;
  if (strlen(AURASYNC_WIFI_SSID) > 0 && WiFi.status() != WL_CONNECTED
      && millis() - lastReconnectAttempt > 30000UL) {
    lastReconnectAttempt = millis();
    WiFi.disconnect();
    WiFi.begin(AURASYNC_WIFI_SSID, AURASYNC_WIFI_PASSWORD);
  }
  delay(2);
}

