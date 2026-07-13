#pragma once

// ================================================================
// AuraSync Showcase Firmware - user-editable configuration
// Target: ESP32-S3 N8R8 + INMP441, Arduino-ESP32 core 3.x
// ================================================================

#define AURASYNC_FIRMWARE_VERSION "2.0.0-showcase"
#define AURASYNC_DEVICE_ID "aurasync_1"

// Optional connection to an existing Wi-Fi network.
// Leave both empty and connect directly to the AuraSync access point.
#define AURASYNC_WIFI_SSID ""
#define AURASYNC_WIFI_PASSWORD ""

// The device always creates this local access point for demonstrations.
#define AURASYNC_AP_SSID "AuraSync-Showcase"
#define AURASYNC_AP_PASSWORD "AuraSync2026"

// INMP441 wiring for the supplied hardware plan.
#define AURASYNC_I2S_BCLK_PIN 14
#define AURASYNC_I2S_WS_PIN 15
#define AURASYNC_I2S_DATA_PIN 16

// Set to 1 when INMP441 L/R is tied to GND (left channel).
// Set to 0 when L/R is tied to 3.3 V (right channel).
#define AURASYNC_MIC_USE_LEFT_CHANNEL 1

// INMP441 24-bit samples arrive inside a 32-bit I2S word.
#define AURASYNC_MIC_SHIFT_BITS 14
#define AURASYNC_MIC_DIGITAL_GAIN 1.0f

// A one-point default offset is supplied only to make first boot viewable.
// Calibrate against a reference sound-level meter before reporting results.
#define AURASYNC_DEFAULT_CAL_OFFSET_DB 119.0f

// Showcase simulation is always visibly labelled and never mixed with real data.
#define AURASYNC_ALLOW_SIMULATION 1
#define AURASYNC_AUTO_SIMULATE_IF_MIC_FAILS 1

// Dashboard refresh rate and local API settings.
#define AURASYNC_DASHBOARD_REFRESH_MS 500

// Provisional showcase fusion weights. They sum to 1.0.
// Replace them only after validation/human-rating weight selection.
#define AURASYNC_WEIGHT_LOUDNESS 0.45f
#define AURASYNC_WEIGHT_SPEECH 0.25f
#define AURASYNC_WEIGHT_PERSISTENCE 0.20f
#define AURASYNC_WEIGHT_IMPULSIVENESS 0.10f

// State thresholds. LOUD and SPIKE are DSP-only outputs.
#define AURASYNC_PREWARNING_SCORE 45.0f
#define AURASYNC_WARNING_SCORE 65.0f
#define AURASYNC_LOUD_ENTER_DBA 76.0f
#define AURASYNC_LOUD_EXIT_DBA 72.0f
#define AURASYNC_SPIKE_MIN_PEAK_DB 78.0f
#define AURASYNC_SPIKE_MIN_CREST_DB 10.0f
#define AURASYNC_SPIKE_ABOVE_BASELINE_DB 20.0f

