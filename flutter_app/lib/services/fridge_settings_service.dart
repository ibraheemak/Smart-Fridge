import 'dart:async';
import 'package:firebase_core/firebase_core.dart';
import 'package:firebase_database/firebase_database.dart';
import '../config.dart';
import '../models/fridge_settings.dart';
import 'fridge_session.dart';

/// Two-way sync of the fridge's tunable settings with the ESP32 CH screen,
/// via RTDB `fridges/{id}/settings` (firmware settings_sync.h).
///
/// - The screen writes the whole object with `updated_by:"screen"`; the app
///   sees it live through [stream].
/// - The app writes changed fields with `updated_by:"app"`; the firmware
///   polls every 5 s and applies them (it ignores its own "screen" echoes).
///
/// A synchronous [cached] copy is kept so the app's own alert logic
/// (temperature range, expiry-warning days, alert toggles) uses the same
/// shared values the screen shows.
class FridgeSettingsService {
  static final _rtdb = FirebaseDatabase.instanceFor(
    app: Firebase.app(),
    databaseURL: AppConfig.rtdbUrl,
  );

  static String get _fid => FridgeSession.fridgeId ?? AppConfig.fridgeId;
  static DatabaseReference get _ref =>
      _rtdb.ref('fridges/$_fid/settings');

  /// Latest known settings, always non-null (starts at firmware defaults).
  /// Read by the alert logic without awaiting.
  static FridgeSettings cached = FridgeSettings.defaults;
  static StreamSubscription? _sub;

  /// Start mirroring the RTDB settings into [cached]. Call once after auth.
  static Future<void> init() async {
    await _sub?.cancel();
    _sub = _ref.onValue.listen((event) {
      final v = event.snapshot.value;
      if (v is Map) cached = FridgeSettings.fromMap(v);
    });
  }

  static Future<void> stop() async {
    await _sub?.cancel();
    _sub = null;
    cached = FridgeSettings.defaults;
  }

  /// Live settings for the UI (null until the first RTDB read arrives).
  static Stream<FridgeSettings?> stream() => _ref.onValue.map((e) {
        final v = e.snapshot.value;
        return v is Map ? FridgeSettings.fromMap(v) : null;
      });

  /// Write the settings back, tagged `updated_by:"app"` so the firmware
  /// applies it. Uses update() (shallow merge) so any field the app doesn't
  /// know about is preserved.
  static Future<void> write(FridgeSettings s) async {
    await _ref.update({
      ...s.toMap(),
      'updated_by': 'app',
      'updated_at': ServerValue.timestamp,
    });
  }
}
