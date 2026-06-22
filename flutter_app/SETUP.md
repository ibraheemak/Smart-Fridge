# Smart Fridge App — Setup Guide

## Prerequisites

| Tool | Version | Install |
|---|---|---|
| Flutter | 3.x | https://docs.flutter.dev/get-started/install |
| Java | 17 | `brew install openjdk@17` (Mac) |
| Android Studio | latest | https://developer.android.com/studio |

**Mac only — after installing Java 17**, add to your `~/.zshrc`:
```bash
export JAVA_HOME=/opt/homebrew/opt/openjdk@17/libexec/openjdk.jdk/Contents/Home
```
Then restart your terminal.

---

## Step 1 — Clone the repo & switch to the Flutter branch

```bash
git clone https://github.com/ibraheemak/Smart-Fridge.git
cd Smart-Fridge
git checkout feature/flutter-app
cd flutter_app
```

---

## Step 2 — Get `google-services.json` from Firebase

This file is **not in git** (gitignored for security). You need it to connect to Firestore and Auth.

Ask a teammate for the file, or download it yourself:

1. Go to **https://console.firebase.google.com** → project **smartfridge-79217**
2. Click the ⚙️ gear → **Project Settings** → scroll to **Your apps**
3. Click the Android app → **Download google-services.json**
4. Place it here:
   ```
   flutter_app/android/app/google-services.json
   ```

---

## Step 3 — Enable Firebase Email/Password Auth (one-time, team admin only)

> Skip if already done. The login screen will show an error if this hasn't been enabled.

1. Firebase Console → **Authentication** → **Sign-in method**
2. Enable **Email/Password** → Save

---

## Step 4 — Install Flutter packages

```bash
flutter pub get
```

---

## Step 5 — Run the app

Connect an Android phone via USB (developer mode on), or start an Android emulator, then:

```bash
flutter run
```

To build an APK to share or install manually:
```bash
flutter build apk --debug
# Output: build/app/outputs/flutter-apk/app-debug.apk
```

---

## App overview

### 5 main tabs
| Tab | What it does |
|---|---|
| **Home** | Dashboard — status cards, live fridge banner, quick actions, recent scans |
| **Inventory** | All fridge items in a bento grid with freshness badges |
| **Recipes** | Gemini AI recipe suggestions from current inventory |
| **Shopping** | Shopping list (auto + manual) with AI suggestions and budget estimate |
| **Settings** | Door/temp/camera/expiry settings + analytics, user management, logout |

### Sub-screens (pushed from the main tabs)
| Screen | How to reach it |
|---|---|
| Expiry Alerts | Home → quick actions, or Inventory banner |
| Temperature Monitor | Home → temperature card, or Settings |
| Analytics | Settings |
| Live Camera View | Home → quick actions, or Settings |
| User Management | Settings |
| Login | Shown automatically when not signed in |

---

## Firestore paths used

| Path | Purpose |
|---|---|
| `fridges/fridge1/inventory/current` | Live inventory (real-time stream) |
| `fridges/fridge1/sensors/temperature` | Temperature & humidity (real-time stream) |
| `fridges/fridge1/scans/*` | Scan history (last 20 scans) |
| `basic-items/basic-items` | Canonical item names for shopping list |

---

## Key config (`lib/config.dart`)

Most values are already set. The only thing to change per environment:

```dart
// Set to the ESP32-CAM's IP on your local WiFi.
// Find it in Arduino Serial Monitor after flashing the CAM:
// look for: [WEB] http://192.168.x.x/latest.jpg
static const String esp32CamBaseUrl = 'http://192.168.1.100';
```

The Gemini API key and Firebase project IDs are already in place.
