import 'package:flutter_local_notifications/flutter_local_notifications.dart';
import 'package:timezone/data/latest_all.dart' as tzdata;
import 'package:timezone/timezone.dart' as tz;
import '../models/fridge_item.dart';
import 'fridge_settings_service.dart';

/// On-device notifications for the three alert stories:
///  - Expiry (US #6): OS-scheduled, so they fire even when the app is closed.
///  - Temperature (US #9) and Door (US #10): fired live while the app runs
///    (real-time conditions can't be pre-scheduled). Always-on push for these
///    needs FCM + a Cloud Function — see docs/PUSH_NOTIFICATIONS_PLAN.md.
class NotificationService {
  static final _plugin = FlutterLocalNotificationsPlugin();
  static bool _ready = false;

  // Stable notification IDs so re-firing an alert replaces the old one
  // instead of stacking duplicates.
  static const _tempId = 900001;
  static const _doorId = 900002;
  static const _expiryBase = 910000; // + item index

  // Don't re-notify the same live condition more than once per window.
  static DateTime? _lastTempAlert;
  static DateTime? _lastDoorAlert;
  static const _liveCooldown = Duration(minutes: 30);

  static const _expiryChannel = AndroidNotificationDetails(
    'expiry_alerts',
    'Expiry Alerts',
    channelDescription: 'Reminders before food expires',
    importance: Importance.high,
    priority: Priority.high,
  );
  static const _sensorChannel = AndroidNotificationDetails(
    'sensor_alerts',
    'Fridge Alerts',
    channelDescription: 'Temperature and door alerts',
    importance: Importance.high,
    priority: Priority.high,
  );

  static Future<void> init() async {
    if (_ready) return;
    tzdata.initializeTimeZones();
    // Local timezone: the ESP32 fleet runs Asia/Jerusalem; use it so
    // scheduled times line up with the fridge's clock.
    try {
      tz.setLocalLocation(tz.getLocation('Asia/Jerusalem'));
    } catch (_) {
      tz.setLocalLocation(tz.getLocation('UTC'));
    }

    const settings = InitializationSettings(
      android: AndroidInitializationSettings('@mipmap/ic_launcher'),
      iOS: DarwinInitializationSettings(),
    );
    await _plugin.initialize(settings);

    await _plugin
        .resolvePlatformSpecificImplementation<
            AndroidFlutterLocalNotificationsPlugin>()
        ?.requestNotificationsPermission();
    await _plugin
        .resolvePlatformSpecificImplementation<
            IOSFlutterLocalNotificationsPlugin>()
        ?.requestPermissions(alert: true, badge: true, sound: true);

    _ready = true;
  }

  /// Reschedule expiry reminders for the whole inventory. Cancels the old
  /// batch first so removed/edited items don't leave stale reminders.
  /// Call whenever inventory changes.
  static Future<void> scheduleExpiryAlerts(List<FridgeItem> items) async {
    if (!_ready) return;
    // Clear previous expiry schedule (IDs in the _expiryBase range).
    final pending = await _plugin.pendingNotificationRequests();
    for (final p in pending) {
      if (p.id >= _expiryBase && p.id < _expiryBase + 10000) {
        await _plugin.cancel(p.id);
      }
    }

    // Honor the shared expiry-alert toggle from the fridge settings.
    if (!FridgeSettingsService.cached.expiryAlertOn) return;

    final warningDays = FridgeSettingsService.cached.expiryWarnDays;
    final now = tz.TZDateTime.now(tz.local);
    var idx = 0;
    for (final item in items) {
      final next = item.nextExpiry;
      if (next == null) continue;
      // Fire `warningDays` before expiry, at 9am local.
      final fireDay = next.subtract(Duration(days: warningDays));
      var when = tz.TZDateTime(
          tz.local, fireDay.year, fireDay.month, fireDay.day, 9);
      // If that moment already passed but the item hasn't expired yet,
      // remind at the next top-of-hour so the user still gets one.
      if (when.isBefore(now) && next.isAfter(DateTime.now())) {
        when = now.add(const Duration(minutes: 1));
      }
      if (when.isBefore(now)) continue; // already expired — no reminder

      final days = next.difference(DateTime.now()).inDays;
      await _plugin.zonedSchedule(
        _expiryBase + idx,
        'Expiring soon: ${item.displayName}',
        days <= 0
            ? '${item.displayName} expires today'
            : '${item.displayName} expires in $days day${days == 1 ? '' : 's'}',
        when,
        const NotificationDetails(android: _expiryChannel, iOS: DarwinNotificationDetails()),
        androidScheduleMode: AndroidScheduleMode.inexactAllowWhileIdle,
        uiLocalNotificationDateInterpretation:
            UILocalNotificationDateInterpretation.absoluteTime,
      );
      idx++;
    }
  }

  /// Fire a temperature alert now (debounced). Call from the temperature
  /// stream when isAlert becomes true.
  static Future<void> notifyTemperature(double tempC) async {
    if (!_ready) return;
    if (_within(_lastTempAlert)) return;
    _lastTempAlert = DateTime.now();
    await _plugin.show(
      _tempId,
      'Temperature alert',
      'Fridge is ${tempC.toStringAsFixed(1)}°C — outside the safe range.',
      const NotificationDetails(android: _sensorChannel, iOS: DarwinNotificationDetails()),
    );
  }

  /// Fire a door-open alert now (debounced). Call when the door has been
  /// open past the alert threshold.
  static Future<void> notifyDoorOpen() async {
    if (!_ready) return;
    if (_within(_lastDoorAlert)) return;
    _lastDoorAlert = DateTime.now();
    await _plugin.show(
      _doorId,
      'Door left open',
      'The fridge door has been open for a while.',
      const NotificationDetails(android: _sensorChannel, iOS: DarwinNotificationDetails()),
    );
  }

  /// Clear the temperature/door debounce when the condition resolves, so the
  /// next breach notifies immediately.
  static void clearTemperature() => _lastTempAlert = null;
  static void clearDoor() => _lastDoorAlert = null;

  static bool _within(DateTime? last) =>
      last != null && DateTime.now().difference(last) < _liveCooldown;
}
