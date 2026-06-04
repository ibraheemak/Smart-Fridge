#pragma once

#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "mbedtls/base64.h"
#include "parameters.h"
#include "SECRETS.h"

String sendToGemini(uint8_t* photo_data, size_t photo_size, const String& basic_items) {
  String prompt =
    "Analyze this refrigerator image. Identify all visible food items and estimate quantities.";
  if (basic_items.length() > 0)
    prompt += " When an item matches one of these canonical names use it exactly: [" + basic_items +
              "]. For any item NOT in that list, describe it in lowercase.";
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
