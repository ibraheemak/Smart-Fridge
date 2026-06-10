/// App configuration — fill in before running.
///
/// Gemini key: same as GEMINI_API_KEY in your SECRETS.h
/// ESP32 IP: shown in serial monitor on CAM boot: [WEB] http://192.168.x.x/latest.jpg
class AppConfig {
  // Gemini AI — recipe recommendations (same key as SECRETS.h)
  static const String geminiApiKey = 'YOUR_GEMINI_API_KEY';
  static const String geminiEndpoint =
      'https://generativelanguage.googleapis.com/v1beta/models/gemini-2.0-flash:generateContent';

  // ESP32-CAM web server — phone must be on the same WiFi as the fridge
  // Format: 'http://192.168.x.x'  (no trailing slash, no /latest.jpg)
  static const String esp32CamBaseUrl = 'http://192.168.1.100';

  // Must match FRIDGE_ID in parameters.h
  static const String fridgeId = 'fridge1';

  // Firebase Storage bucket (from parameters.h FIREBASE_STORAGE_BUCKET)
  static const String storageBucket = 'smartfridge-79217.firebasestorage.app';
}
