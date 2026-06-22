#pragma once
// Firebase Storage uploader.
// Uses Firebase Anonymous Auth to get a short-lived ID token, then uploads
// the scan JPEG to snapshots/latest.jpg in Firebase Storage.
//
// Requires Firebase Storage rules to allow authenticated writes:
//   match /snapshots/{f} { allow read: true; allow write: if request.auth != null; }
// Anonymous sign-in satisfies request.auth != null.

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "SECRETS.h"
#include "parameters.h"

// ID token cache — refreshed when expired (~58 min)
static String        _storageToken   = "";
static unsigned long _tokenExpireMs  = 0;

// Obtain an anonymous Firebase ID token via Identity Toolkit REST API.
static bool _refreshStorageToken() {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(8000);
  String url = "https://identitytoolkit.googleapis.com/v1/accounts:signInAnonymously?key=" +
               String(FIREBASE_API_KEY);
  if (!http.begin(client, url)) return false;
  http.addHeader("Content-Type", "application/json");
  int code = http.POST("{\"returnSecureToken\":true}");
  if (code != 200) {
    Serial.printf("[STORAGE] Auth failed HTTP %d\n", code);
    http.end();
    return false;
  }
  DynamicJsonDocument doc(2048);
  deserializeJson(doc, http.getString());
  http.end();
  _storageToken  = doc["idToken"].as<String>();
  _tokenExpireMs = millis() + 3500000UL;  // refresh 100s before 1-hour expiry
  bool ok = _storageToken.length() > 0;
  Serial.printf("[STORAGE] Auth %s\n", ok ? "OK" : "empty token");
  return ok;
}

static bool _ensureStorageToken() {
  if (_storageToken.length() > 0 && millis() < _tokenExpireMs) return true;
  return _refreshStorageToken();
}

// Upload JPEG bytes to Firebase Storage at snapshots/latest.jpg.
// Returns the public download URL, or "" on failure.
String uploadSnapshot(const uint8_t* buf, size_t len) {
  if (!_ensureStorageToken()) return "";

  String url = "https://firebasestorage.googleapis.com/v0/b/" +
               String(FIREBASE_STORAGE_BUCKET) +
               "/o?name=snapshots%2Flatest.jpg&uploadType=media";

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(30000);  // Storage uploads can be slow on congested WiFi
  if (!http.begin(client, url)) return "";
  http.addHeader("Content-Type", "image/jpeg");
  http.addHeader("Authorization", "Bearer " + _storageToken);
  int code = http.POST(const_cast<uint8_t*>(buf), len);
  if (code != 200) {
    Serial.printf("[STORAGE] Upload failed HTTP %d\n", code);
    if (code == 401 || code == 403) _storageToken = "";  // force re-auth next time
    http.end();
    return "";
  }

  DynamicJsonDocument doc(1024);
  deserializeJson(doc, http.getString());
  http.end();

  String token = doc["downloadTokens"].as<String>();
  if (token.length() == 0) {
    Serial.println("[STORAGE] Upload OK but no downloadTokens in response");
    return "";
  }

  String dlUrl = "https://firebasestorage.googleapis.com/v0/b/" +
                 String(FIREBASE_STORAGE_BUCKET) +
                 "/o/snapshots%2Flatest.jpg?alt=media&token=" + token;
  Serial.printf("[STORAGE] Upload OK — %u bytes\n", (unsigned)len);
  return dlUrl;
}
