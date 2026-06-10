// SETUP REQUIRED — see SETUP.md for step-by-step instructions.
// Replace every 'FILL_IN_...' value with your Firebase project credentials.
// Firebase Console → Project Settings → General → Your apps → Add app (Android/iOS)

import 'package:firebase_core/firebase_core.dart' show FirebaseOptions;
import 'package:flutter/foundation.dart'
    show defaultTargetPlatform, kIsWeb, TargetPlatform;

class DefaultFirebaseOptions {
  static FirebaseOptions get currentPlatform {
    if (kIsWeb) return web;
    switch (defaultTargetPlatform) {
      case TargetPlatform.android:
        return android;
      case TargetPlatform.iOS:
        return ios;
      default:
        throw UnsupportedError(
          'DefaultFirebaseOptions not configured for this platform.',
        );
    }
  }

  // ── Android ────────────────────────────────────────────────────────────────
  // From google-services.json:
  //   api_key[0].current_key        → apiKey
  //   client[0].client_info.mobilesdk_app_id → appId
  //   project_info.project_number   → messagingSenderId
  static const FirebaseOptions android = FirebaseOptions(
    apiKey: 'AIzaSyCpScudaWplabSgjN98XHTpJW9en0o6Sz0',
    appId: '1:657433954494:android:bf402e0be7dac0cfca36ef',
    messagingSenderId: '657433954494',
    projectId: 'smartfridge-79217',
    storageBucket: 'smartfridge-79217.firebasestorage.app',
  );

  // ── iOS ────────────────────────────────────────────────────────────────────
  // From GoogleService-Info.plist:
  //   API_KEY       → apiKey
  //   GOOGLE_APP_ID → appId
  //   GCM_SENDER_ID → messagingSenderId
  //   BUNDLE_ID     → iosBundleId
  static const FirebaseOptions ios = FirebaseOptions(
    apiKey: 'FILL_IN_IOS_API_KEY',
    appId: 'FILL_IN_IOS_APP_ID',
    messagingSenderId: '657433954494',
    projectId: 'smartfridge-79217',
    storageBucket: 'smartfridge-79217.firebasestorage.app',
    iosBundleId: 'com.smartfridge.app',
  );

  // ── Web ────────────────────────────────────────────────────────────────────
  static const FirebaseOptions web = FirebaseOptions(
    apiKey: 'AIzaSyBjZFC8OVoeO3fJoCRi2usTGYIKH6rk9So',
    appId: '1:657433954494:web:f7c2e08523425d86ca36ef',
    messagingSenderId: '657433954494',
    projectId: 'smartfridge-79217',
    storageBucket: 'smartfridge-79217.firebasestorage.app',
    authDomain: 'smartfridge-79217.firebaseapp.com',
  );
}
