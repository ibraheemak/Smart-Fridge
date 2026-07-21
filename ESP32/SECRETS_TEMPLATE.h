// ============================================================================
// SECRETS.h  —  TEMPLATE (no real credentials)
// ============================================================================
// This template lists every credential the firmware expects. The real,
// filled-in SECRETS.h is intentionally NOT committed (see .gitignore).
//
// HOW TO USE
//   1. Copy this file into EACH ESP32 sketch folder that needs credentials:
//         ESP32/SmartFridge_ESP32_CH/SECRETS.h
//         ESP32/SmartFridge_ESP32_CAM/SECRETS.h
//   2. Rename each copy to  SECRETS.h  and fill in your real values below.
//   3. Do NOT commit the filled-in SECRETS.h — it is already gitignored.
//
// If your real SECRETS.h defines any additional field not listed here,
// add it here too so the template stays complete.
// ============================================================================

#pragma once

// --- Firebase project (Firestore + Realtime Database) -----------------------
#define FIREBASE_PROJECT_ID   ""   // e.g. "smartfridge-xxxxx"
#define FIREBASE_API_KEY      ""   // Firebase Web API key

// --- Google Gemini (AI vision item detection + recipe suggestions) ----------
#define GEMINI_API_KEY        ""   // Google AI Studio API key

// --- OpenAI GPT (fallback used when Gemini is unavailable) ------------------
#define OPENAI_API_KEY        ""   // OpenAI API key (sk-...)

// --- Optional: add any extra project-specific fields below ------------------
// #define FIREBASE_RTDB_URL  ""   // Realtime Database URL, if hard-coded here
