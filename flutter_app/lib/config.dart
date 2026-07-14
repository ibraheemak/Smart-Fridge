import 'app_secrets.dart';

/// App configuration — fill in before running.
///
/// Gemini key: copy app_secrets.dart.example → app_secrets.dart and fill in key.
/// ESP32 IP: shown in serial monitor on CAM boot: [WEB] http://192.168.x.x/latest.jpg
class AppConfig {
  // Gemini AI — recipe recommendations (same key as SECRETS.h)
  static const String geminiApiKey = AppSecrets.geminiApiKey;
  static const String geminiEndpoint =
      'https://generativelanguage.googleapis.com/v1beta/models/gemini-2.0-flash:generateContent';

  // Image generation endpoint for food icons (Gemini image generation model)
  static const String geminiImageEndpoint =
      'https://generativelanguage.googleapis.com/v1beta/models/gemini-2.0-flash-preview-image-generation:generateContent';

  // Must match FRIDGE_ID in parameters.h
  static const String fridgeId = 'fridge1';

  // Firebase Storage bucket (from parameters.h FIREBASE_STORAGE_BUCKET)
  static const String storageBucket = 'smartfridge-79217.firebasestorage.app';

  // Firebase Realtime Database — used for the Live View request trigger
  // (must match FIREBASE_DATABASE_URL in ESP32 SECRETS.h). firebase_options.dart
  // doesn't set databaseURL, so FridgeService looks it up here explicitly via
  // FirebaseDatabase.instanceFor(...).
  static const String rtdbUrl = 'https://smartfridge-79217-default-rtdb.firebaseio.com';

  // Must match NUM_ROOFS in the CH board's parameters.h — one ESP32-CAM per
  // fridge shelf, Live View lets the user switch between them.
  static const int numRoofs = 2;
}
