#pragma once

#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "mbedtls/base64.h"
#include "parameters.h"
#include "SECRETS.h"

String sendToGemini(uint8_t* photo_data, size_t photo_size, const String& basic_items,
                     const String& known_items) {
  String prompt =
    "Analyze this refrigerator image. Identify all visible food items and estimate quantities.";
  if (basic_items.length() > 0)
    prompt += " When an item matches one of these canonical names use it exactly: [" + basic_items +
              "]. For any item NOT in that list, describe it in lowercase.";
  if (known_items.length() > 0)
    prompt += " These items are already tracked in this fridge from a previous scan: [" + known_items +
              "]. If what you see is the same item, reuse that EXACT name text (same wording, same word"
              " order) instead of describing it differently.";
#if DEBUG_MODE
  prompt += " Also add a top-level \\\"description\\\" field with a brief note on image quality and lighting.";
#endif
  prompt += " Return valid JSON only: {\\\"items\\\": [{\\\"name\\\": \\\"item name\\\","
            " \\\"quantity\\\": \\\"amount\\\", \\\"confidence\\\": \\\"high/medium/low\\\"}]}";

  const char* mid  = "\"},{\"inlineData\":{\"mimeType\":\"image/jpeg\",\"data\":\"";
  const char* tail = "\"}}]}]}";
  String prefix_str = String("{\"contents\":[{\"parts\":[{\"text\":\"") + prompt + mid;
  size_t prefix_len = prefix_str.length(), tail_len = strlen(tail);

  size_t enc_len = 0;
  mbedtls_base64_encode(nullptr, 0, &enc_len, photo_data, photo_size);

  char* body = (char*)malloc(prefix_len + enc_len + tail_len + 1);
  if (!body) { Serial.println("[GEMINI] alloc failed"); return ""; }

  memcpy(body, prefix_str.c_str(), prefix_len);
  size_t actual_enc = 0;
  if (mbedtls_base64_encode((unsigned char*)(body + prefix_len), enc_len,
                            &actual_enc, photo_data, photo_size) != 0) {
    free(body); return "";
  }
  memcpy(body + prefix_len + actual_enc, tail, tail_len);
  size_t body_len = prefix_len + actual_enc + tail_len;
  body[body_len] = '\0';

  String url = String(GEMINI_API_ENDPOINT) + "?key=" + GEMINI_API_KEY;
  String response = "";

  for (int attempt = 1; attempt <= GEMINI_MAX_RETRIES + 1; attempt++) {
    HTTPClient http;
    http.begin(url);
    http.setTimeout(GEMINI_REQUEST_TIMEOUT_MS);
    http.addHeader("Content-Type", "application/json");
    int code = http.POST((uint8_t*)body, body_len);
    if (code == 200) {
      response = http.getString();
      http.end();
      break;
    } else if (code == 503 && attempt <= GEMINI_MAX_RETRIES) {
      Serial.printf("[GEMINI] 503 — retrying (%d/%d)\n", attempt, GEMINI_MAX_RETRIES + 1);
      http.end();
      delay(5000);
    } else {
      Serial.printf("[GEMINI] HTTP %d\n", code);
      http.end();
      break;
    }
  }
  free(body);
  return response;
}

bool parseGeminiResponse(const String& response, JsonDocument& detected_items) {
  StaticJsonDocument<8192> full_response;
  if (deserializeJson(full_response, response)) return false;

  const char* text = full_response["candidates"][0]["content"]["parts"][0]["text"];
  if (!text) return false;

  String json_text = String(text);
  json_text.trim();
  if (json_text.startsWith("```")) {
    int nl = json_text.indexOf('\n');
    if (nl >= 0) json_text = json_text.substring(nl + 1);
    int fence = json_text.lastIndexOf("```");
    if (fence >= 0) json_text = json_text.substring(0, fence);
    json_text.trim();
  }
  int fb = json_text.indexOf('{'), lb = json_text.lastIndexOf('}');
  if (fb >= 0 && lb > fb) json_text = json_text.substring(fb, lb + 1);

  return !deserializeJson(detected_items, json_text);
}

// ----------------------------------------------------------------------------
// GPT (OpenAI) fallback — used when Gemini errors out or returns something
// unparseable, or directly when AI_FORCE_GPT is set (debug: skip Gemini
// entirely and always call GPT, to test the fallback path itself).
// Mirrors sendToGemini()'s raw string-building approach (prefix/base64/tail)
// instead of ArduinoJson, since the base64 photo payload is too big to want
// a second in-memory copy inside a JsonDocument.
// ----------------------------------------------------------------------------
String sendToGPT(uint8_t* photo_data, size_t photo_size, const String& basic_items,
                  const String& known_items) {
  String prompt =
    "Analyze this refrigerator image. Identify all visible food items and estimate quantities.";
  if (basic_items.length() > 0)
    prompt += " When an item matches one of these canonical names use it exactly: [" + basic_items +
              "]. For any item NOT in that list, describe it in lowercase.";
  if (known_items.length() > 0)
    prompt += " These items are already tracked in this fridge from a previous scan: [" + known_items +
              "]. If what you see is the same item, reuse that EXACT name text (same wording, same word"
              " order) instead of describing it differently.";
#if DEBUG_MODE
  prompt += " Also add a top-level \\\"description\\\" field with a brief note on image quality and lighting.";
#endif
  prompt += " Return valid JSON only: {\\\"items\\\": [{\\\"name\\\": \\\"item name\\\","
            " \\\"quantity\\\": \\\"amount\\\", \\\"confidence\\\": \\\"high/medium/low\\\"}]}";

  const char* mid  = "\"},{\"type\":\"image_url\",\"image_url\":{\"url\":\"data:image/jpeg;base64,";
  const char* tail = "\"}}]}],\"max_tokens\":1000}";
  String prefix_str = String("{\"model\":\"") + OPENAI_VISION_MODEL +
                       "\",\"messages\":[{\"role\":\"user\",\"content\":[{\"type\":\"text\",\"text\":\"" +
                       prompt + mid;
  size_t prefix_len = prefix_str.length(), tail_len = strlen(tail);

  size_t enc_len = 0;
  mbedtls_base64_encode(nullptr, 0, &enc_len, photo_data, photo_size);

  char* body = (char*)malloc(prefix_len + enc_len + tail_len + 1);
  if (!body) { Serial.println("[GPT] alloc failed"); return ""; }

  memcpy(body, prefix_str.c_str(), prefix_len);
  size_t actual_enc = 0;
  if (mbedtls_base64_encode((unsigned char*)(body + prefix_len), enc_len,
                            &actual_enc, photo_data, photo_size) != 0) {
    free(body); return "";
  }
  memcpy(body + prefix_len + actual_enc, tail, tail_len);
  size_t body_len = prefix_len + actual_enc + tail_len;
  body[body_len] = '\0';

  HTTPClient http;
  http.begin(OPENAI_API_ENDPOINT);
  http.setTimeout(OPENAI_REQUEST_TIMEOUT_MS);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", String("Bearer ") + OPENAI_API_KEY);
  int code = http.POST((uint8_t*)body, body_len);
  free(body);

  String response = "";
  if (code == 200) {
    response = http.getString();
  } else {
    Serial.printf("[GPT] HTTP %d\n", code);
  }
  http.end();
  return response;
}

bool parseGPTResponse(const String& response, JsonDocument& detected_items) {
  StaticJsonDocument<8192> full_response;
  if (deserializeJson(full_response, response)) return false;

  const char* text = full_response["choices"][0]["message"]["content"];
  if (!text) return false;

  String json_text = String(text);
  json_text.trim();
  if (json_text.startsWith("```")) {
    int nl = json_text.indexOf('\n');
    if (nl >= 0) json_text = json_text.substring(nl + 1);
    int fence = json_text.lastIndexOf("```");
    if (fence >= 0) json_text = json_text.substring(0, fence);
    json_text.trim();
  }
  int fb = json_text.indexOf('{'), lb = json_text.lastIndexOf('}');
  if (fb >= 0 && lb > fb) json_text = json_text.substring(fb, lb + 1);

  return !deserializeJson(detected_items, json_text);
}

// ----------------------------------------------------------------------------
// Single entry point used by the .ino / offline_buffer.h: tries Gemini first
// (unless AI_FORCE_GPT debug flag forces GPT straight away), falls back to
// GPT if Gemini comes back empty or fails to parse.
// ----------------------------------------------------------------------------
bool detectItemsFromPhoto(uint8_t* photo_data, size_t photo_size, const String& basic_items,
                           const String& known_items, JsonDocument& detected_items) {
#if AI_FORCE_GPT
  Serial.println("[AI] AI_FORCE_GPT set — calling GPT directly");
  String response = sendToGPT(photo_data, photo_size, basic_items, known_items);
  if (response.length() == 0) { Serial.println("[AI] GPT returned empty"); return false; }
  return parseGPTResponse(response, detected_items);
#else
  Serial.println("[AI] Sending to Gemini...");
  String response = sendToGemini(photo_data, photo_size, basic_items, known_items);
  if (response.length() > 0 && parseGeminiResponse(response, detected_items)) return true;

  Serial.println("[AI] Gemini failed — falling back to GPT...");
  detected_items.clear();
  String gpt_response = sendToGPT(photo_data, photo_size, basic_items, known_items);
  if (gpt_response.length() == 0) { Serial.println("[AI] GPT returned empty"); return false; }
  return parseGPTResponse(gpt_response, detected_items);
#endif
}
