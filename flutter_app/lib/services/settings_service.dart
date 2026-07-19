import 'package:shared_preferences/shared_preferences.dart';

/// App-side preferences. Every value here is read back by real logic:
///  - min/max temp   → TemperatureReading.isAlert / temperature screen banner
///  - expiry days    → FridgeItem.expiryStatus "critical" threshold
class SettingsService {
  static SharedPreferences? _prefs;

  static Future<void> init() async {
    _prefs ??= await SharedPreferences.getInstance();
  }

  // Safe temperature range for in-app alerts.
  // Defaults match the firmware's alert range (2–8°C).
  static double getMinTemp() => _prefs?.getDouble('minTemp') ?? 2;
  static double getMaxTemp() => _prefs?.getDouble('maxTemp') ?? 8;
  static Future<void> setMinTemp(double v) async =>
      _prefs?.setDouble('minTemp', v);
  static Future<void> setMaxTemp(double v) async =>
      _prefs?.setDouble('maxTemp', v);

  // How many days before an expiry date an item counts as "critical".
  static int getExpiryWarningDays() =>
      _prefs?.getInt('expiryWarningDays') ?? 3;
  static Future<void> setExpiryWarningDays(int v) async =>
      _prefs?.setInt('expiryWarningDays', v);
}
