#pragma once

#include "parameters.h"

// ============================================================================
// Passive buzzer (KY-006) — door-open alert (US #10)
//
// Wiring:
//   "+" / VCC  ->  3.3V
//   "-" / GND  ->  GND
//   "S"        ->  GPIO BUZZER_PIN (14)
//
// Driven with the LEDC PWM peripheral (not tone()) so the Settings screen can
// control BOTH pitch and volume:
//   - pitch  = PWM frequency          (g_buzzer_freq, Hz)
//   - volume = PWM duty cycle          (g_buzzer_volume, 0-100%). A passive
//              piezo is loudest at 50% duty and silent at 0%, so volume% maps
//              linearly onto 0..50% duty.
//   - length = total alert duration    (g_buzzer_duration_ms)
//   - melody = beep pattern preset      (g_buzzer_melody, see MELODIES[])
//
// Volume/pitch/melody only have an audible effect on a *passive* buzzer. An
// active buzzer (built-in oscillator) will just beep on/off and ignore them.
//
// Non-blocking: call updateBuzzer() every loop().
// ============================================================================

// User-toggleable via the Settings screen (see settings.h). When false the
// door-open alert stays completely silent — buzzFor() becomes a no-op.
bool          g_buzzer_enabled     = true;
int           g_buzzer_volume      = 80;                  // 0..100 %
int           g_buzzer_freq        = 2000;               // Hz (pitch)
unsigned long g_buzzer_duration_ms = BUZZER_DURATION_MS; // total alert length
int           g_buzzer_melody      = 0;                  // index into MELODIES[]

// LEDC channel + PWM resolution used to drive the buzzer pin.
#define BUZZER_LEDC_CH   0
#define BUZZER_LEDC_RES  8    // bits -> duty 0..255, 50% = 128

// ----------------------------------------------------------------------------
// Beep-pattern presets. Each melody is: `beeps` short tones of `onMs` each,
// separated by `gapMs`, then a `pauseMs` rest before the group repeats.
// A "siren" melody instead alternates between two pitches every `onMs`.
// ----------------------------------------------------------------------------
struct BuzzMelody {
  const char* name;
  int  onMs;      // tone-on duration (also the swap interval for a siren)
  int  gapMs;     // silence between beeps within a group
  int  beeps;     // beeps per group
  int  pauseMs;   // silence between groups
  bool siren;     // true = alternate two pitches instead of beeping
};

static const BuzzMelody MELODIES[] = {
  {"Beep-beep", 150, 250, 2, 1500, false},
  {"Single",    600,   0, 1, 1200, false},
  {"Fast",      100, 120, 1,  120, false},
  {"Siren",     350,   0, 1,    0, true},
};
#define MELODY_COUNT ((int)(sizeof(MELODIES) / sizeof(MELODIES[0])))

const char* buzzerMelodyName() {
  int i = g_buzzer_melody;
  if (i < 0 || i >= MELODY_COUNT) i = 0;
  return MELODIES[i].name;
}

// ----------------------------------------------------------------------------
// Buzzer polarity
// ----------------------------------------------------------------------------
// Cheap 3-pin buzzer *modules* (VCC/GND/S, with a driver transistor on board)
// are usually ACTIVE-LOW: the "S" pin sounds the buzzer when driven LOW and is
// SILENT when HIGH. A bare piezo wired straight to the GPIO is active-high.
//
// This matters because the LEDC duty cycle sets how much of the time the pin is
// HIGH. If this flag is wrong you get the classic symptoms: volume runs
// backwards (turning it "down" makes it louder) and the buzzer buzzes faintly
// during the silent gaps between beeps. Our board's module is active-low.
#ifndef BUZZER_ACTIVE_LOW
#define BUZZER_ACTIVE_LOW 1
#endif

// ----------------------------------------------------------------------------
// Low-level tone control (LEDC, volume via duty cycle)
// ----------------------------------------------------------------------------
void buzzerPlay(int freq) {
  // volume% -> how hard the pin is driven toward the buzzer's sound-producing
  // state. 255 = loudest (driven continuously), 0 = silent.
  int soundDuty = (255 * constrain(g_buzzer_volume, 0, 100)) / 100;
#if BUZZER_ACTIVE_LOW
  int duty = 255 - soundDuty;   // active-low: pin LOW makes sound, so invert
#else
  int duty = soundDuty;         // active-high: pin HIGH makes sound
#endif
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcChangeFrequency(BUZZER_PIN, freq, BUZZER_LEDC_RES);
  ledcWrite(BUZZER_PIN, duty);
#else
  ledcSetup(BUZZER_LEDC_CH, freq, BUZZER_LEDC_RES);
  ledcWrite(BUZZER_LEDC_CH, duty);
#endif
}

void buzzerSilence() {
  // Drive the pin to the *quiet* state: HIGH for active-low, LOW for active-high.
#if BUZZER_ACTIVE_LOW
  int duty = 255;
#else
  int duty = 0;
#endif
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(BUZZER_PIN, duty);
#else
  ledcWrite(BUZZER_LEDC_CH, duty);
#endif
}

// ----------------------------------------------------------------------------
// Non-blocking playback state machine
// ----------------------------------------------------------------------------
static unsigned long buzzerEndAt  = 0;   // when the whole alert ends (0 = idle)
static unsigned long buzzerNextAt = 0;   // when to advance the pattern next
static int           buzzerBeepIdx = 0;  // which beep in the current group
static bool          buzzerOn      = false;  // is a tone currently sounding
static bool          sirenHigh     = false;  // siren pitch toggle

void updateBuzzer() {
  if (buzzerEndAt == 0) return;

  unsigned long now = millis();
  if (now >= buzzerEndAt) {
    buzzerSilence();
    buzzerEndAt = 0;
    Serial.println("[BUZZER] alert done");
    return;
  }
  if (now < buzzerNextAt) return;

  int mi = (g_buzzer_melody < 0 || g_buzzer_melody >= MELODY_COUNT) ? 0 : g_buzzer_melody;
  const BuzzMelody& m = MELODIES[mi];

  if (m.siren) {
    sirenHigh = !sirenHigh;
    buzzerPlay(sirenHigh ? g_buzzer_freq : (g_buzzer_freq * 2) / 3);
    buzzerNextAt = now + m.onMs;
    return;
  }

  if (!buzzerOn) {
    buzzerPlay(g_buzzer_freq);
    buzzerOn = true;
    buzzerNextAt = now + m.onMs;
  } else {
    buzzerSilence();
    buzzerOn = false;
    buzzerBeepIdx++;
    if (buzzerBeepIdx >= m.beeps) {
      buzzerBeepIdx = 0;
      buzzerNextAt = now + m.pauseMs;
    } else {
      buzzerNextAt = now + m.gapMs;
    }
  }
}

void initBuzzer() {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(BUZZER_PIN, g_buzzer_freq, BUZZER_LEDC_RES);
#else
  ledcSetup(BUZZER_LEDC_CH, g_buzzer_freq, BUZZER_LEDC_RES);
  ledcAttachPin(BUZZER_PIN, BUZZER_LEDC_CH);
#endif
  buzzerSilence();   // start in the quiet state (respects BUZZER_ACTIVE_LOW)
  Serial.printf("[BUZZER] init — GPIO%d (LEDC)\n", BUZZER_PIN);
}

// Start the alert for durationMs total. Safe to call repeatedly.
// No-op when the user has muted the buzzer in Settings, unless `force` is set
// (used by the Settings "Test" button so you can preview even while muted).
void buzzFor(unsigned long durationMs, bool force = false) {
  if (!force && !g_buzzer_enabled) return;
  buzzerEndAt   = millis() + durationMs;
  buzzerNextAt  = millis();
  buzzerBeepIdx = 0;
  buzzerOn      = false;
  sirenHigh     = false;
  Serial.printf("[BUZZER] alert start — %lu ms, %s @ %dHz vol %d%%\n",
                durationMs, buzzerMelodyName(), g_buzzer_freq, g_buzzer_volume);
}
