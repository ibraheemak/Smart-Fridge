#pragma once

#include <Preferences.h>
#include <WiFiManager.h>
#include "parameters.h"

// ============================================================================
// On-demand WiFi config portal.
//
// The user can re-point this camera at a different router from the CH board's
// Settings > WiFi screen, without a serial cable and without holding BOOT to
// wipe the credentials. CH unicasts "WIFI_PORTAL" over ESP-NOW (see
// espnow_link.h); this board persists a one-shot "open the portal on the next
// boot" flag in NVS and restarts.
//
// Why a flag + reboot instead of opening the portal right where the message
// arrives: WiFiManager's portal is blocking and wants the radio to itself, and
// the sketch would be mid-loop with the camera initialized, an RTDB stream
// open and possibly a scan in flight. Rebooting gives the portal the same
// clean, pre-camera radio state the normal boot path already hands it, so the
// portal code path is the one already proven to work at startup.
//
// The flag is consumed (cleared) as it's read, so a portal that times out
// without the user entering anything doesn't trap the board in a setup loop —
// it just boots normally next time, retrying its saved credentials.
// ============================================================================

// AP the portal hosts, made unique per roof so a user with several cameras can
// tell which one they're configuring (WIFI_AP_NAME is the shared prefix).
String wifiPortalApName() {
  return String(WIFI_AP_NAME) + String(CAMERA_ROOF);
}

// Reads and clears the one-shot flag. Call once at boot.
bool takeWifiPortalRequest() {
  Preferences p;
  p.begin("wifiportal", false);
  bool pending = p.getBool("pending", false);
  if (pending) p.putBool("pending", false);
  p.end();
  return pending;
}

// Sets the flag and reboots straight into the config portal.
void rebootIntoWifiPortal() {
  Preferences p;
  p.begin("wifiportal", false);
  p.putBool("pending", true);
  p.end();
  Serial.println("[WIFI] Portal requested — restarting into setup mode");
  delay(200);
  ESP.restart();
}
