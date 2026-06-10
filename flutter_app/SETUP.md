# Smart Fridge App — Setup Guide

## 1. Install Flutter (if not already)

https://docs.flutter.dev/get-started/install

## 2. Generate native project files

Run once inside `flutter_app/`:

```bash
cd flutter_app
flutter create . --project-name smart_fridge --no-overwrite
flutter pub get
```

## 3. Get google-services.json (Android) from Firebase

1. Go to **https://console.firebase.google.com**
2. Open project **smartfridge-79217**
3. Click the gear icon → **Project Settings**
4. Scroll to **Your apps** → click **Add app** → choose **Android**
5. Package name: `com.smartfridge.app` (or whatever you like)
6. Click **Register app**
7. Download **google-services.json**
8. Place it at:
   ```
   flutter_app/android/app/google-services.json
   ```

## 4. Get GoogleService-Info.plist (iOS) from Firebase — optional

Same steps but choose **iOS** app and download **GoogleService-Info.plist**.  
Place it at `flutter_app/ios/Runner/GoogleService-Info.plist` using Xcode.

## 5. Fill in firebase_options.dart

Open the downloaded **google-services.json** and copy these values into  
`lib/firebase_options.dart` → `android` section:

| google-services.json field                        | firebase_options.dart field |
|---------------------------------------------------|-----------------------------|
| `client[0].api_key[0].current_key`                | `apiKey`                    |
| `client[0].client_info.mobilesdk_app_id`          | `appId`                     |
| `project_info.project_number`                     | `messagingSenderId`         |

`projectId` and `storageBucket` are already filled in.

## 6. Fill in config.dart

Open `lib/config.dart` and set:

```dart
// Your Gemini API key — same as GEMINI_API_KEY in SECRETS.h
static const String geminiApiKey = 'YOUR_ACTUAL_KEY_HERE';

// Your ESP32-CAM's IP address shown in Serial Monitor on boot:
// [WEB] http://192.168.x.x/latest.jpg
static const String esp32CamBaseUrl = 'http://192.168.1.xxx';
```

## 7. Allow HTTP (camera image is served over HTTP, not HTTPS)

In `android/app/src/main/AndroidManifest.xml` add  
`android:usesCleartextTraffic="true"` to the `<application>` tag:

```xml
<application
    android:usesCleartextTraffic="true"
    ...>
```

## 8. Run the app

```bash
flutter run
```

---

## Screens

| Screen | Description |
|---|---|
| **Fridge** | Live inventory from Firestore — updates instantly after every door-close scan |
| **Camera** | Fetches the latest photo from the ESP32-CAM over WiFi |
| **History** | Timeline of past scans + bar chart of items per scan |
| **Shopping** | Auto-generated shopping list + Gemini AI recipe suggestions |

## Color theme

Warm & Friendly — cream background (#FFF8F0), orange accent (#FF6B35), Poppins font.

## Firestore paths used (read-only)

| Path | Used by |
|---|---|
| `fridges/fridge1/inventory/current` | Inventory screen (real-time listener) |
| `fridges/fridge1/sensors/temperature` | Temperature card (real-time listener) |
| `fridges/fridge1/scans/*` | History screen (last 20 scans) |
| `basic-items/basic-items` | Shopping list reference |
