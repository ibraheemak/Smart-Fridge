import 'package:shared_preferences/shared_preferences.dart';

class SettingsService {
  static SharedPreferences? _prefs;

  static Future<void> init() async {
    _prefs ??= await SharedPreferences.getInstance();
  }

  // Door settings
  static double getDoorOpenTimer() => _prefs?.getDouble('doorOpenTimer') ?? 30;
  static Future<void> setDoorOpenTimer(double v) async =>
      _prefs?.setDouble('doorOpenTimer', v);
  static bool getBuzzerEnabled() => _prefs?.getBool('buzzerEnabled') ?? false;
  static Future<void> setBuzzerEnabled(bool v) async =>
      _prefs?.setBool('buzzerEnabled', v);
  static bool getMobileAlerts() => _prefs?.getBool('mobileAlerts') ?? true;
  static Future<void> setMobileAlerts(bool v) async =>
      _prefs?.setBool('mobileAlerts', v);

  // Temperature settings
  static double getMinTemp() => _prefs?.getDouble('minTemp') ?? 3;
  static double getMaxTemp() => _prefs?.getDouble('maxTemp') ?? 7;
  static Future<void> setMinTemp(double v) async =>
      _prefs?.setDouble('minTemp', v);
  static Future<void> setMaxTemp(double v) async =>
      _prefs?.setDouble('maxTemp', v);
  static bool getTempAlerts() => _prefs?.getBool('tempAlerts') ?? true;
  static Future<void> setTempAlerts(bool v) async =>
      _prefs?.setBool('tempAlerts', v);

  // Camera settings
  static double getScanDelay() => _prefs?.getDouble('scanDelay') ?? 15;
  static Future<void> setScanDelay(double v) async =>
      _prefs?.setDouble('scanDelay', v);
  static bool getAutoScan() => _prefs?.getBool('autoScan') ?? true;
  static Future<void> setAutoScan(bool v) async =>
      _prefs?.setBool('autoScan', v);
  static bool getFlashOnScan() => _prefs?.getBool('flashOnScan') ?? false;
  static Future<void> setFlashOnScan(bool v) async =>
      _prefs?.setBool('flashOnScan', v);

  // Expiry settings
  static int getExpiryWarningDays() =>
      _prefs?.getInt('expiryWarningDays') ?? 3;
  static Future<void> setExpiryWarningDays(int v) async =>
      _prefs?.setInt('expiryWarningDays', v);
  static bool getDailySummary() => _prefs?.getBool('dailySummary') ?? true;
  static Future<void> setDailySummary(bool v) async =>
      _prefs?.setBool('dailySummary', v);
}
