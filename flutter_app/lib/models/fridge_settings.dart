/// All tunable fridge settings, mirrored with the ESP32 CH screen at
/// RTDB `fridges/{id}/settings` (see firmware settings_sync.h). One flat
/// object; the screen writes `updated_by:"screen"`, the app writes
/// `updated_by:"app"`.
class FridgeSettings {
  // Environment safe ranges
  final int tempMin; // °C   (-20 .. tempMax-1)
  final int tempMax; // °C   (tempMin+1 .. 30)
  final int humMin; //  %    (0 .. humMax-5)
  final int humMax; //  %    (humMin+5 .. 100)

  // Door
  final bool buzzerEnabled;
  final int doorAlertS; // seconds the door may stay open (5 .. 300, step 5)

  // Buzzer tuning
  final int buzzerVolume; // 0 .. 100, step 10
  final int buzzerFreq; //   500 .. 4000 Hz, step 100
  final int buzzerDurationS; // 2 .. 60 s, step 2
  final int buzzerMelody; //  0 .. 3 (see melodyNames)

  // Notifications / alerts
  final int notifRetentionD; // 1 .. 90 days
  final int expiryWarnDays; //  0 .. 30 days
  final bool expiryAlertOn;
  final bool expiredAlertOn;
  final bool newitemAlertOn;
  final bool envAlertOn;
  final bool doorAlertOn;

  const FridgeSettings({
    required this.tempMin,
    required this.tempMax,
    required this.humMin,
    required this.humMax,
    required this.buzzerEnabled,
    required this.doorAlertS,
    required this.buzzerVolume,
    required this.buzzerFreq,
    required this.buzzerDurationS,
    required this.buzzerMelody,
    required this.notifRetentionD,
    required this.expiryWarnDays,
    required this.expiryAlertOn,
    required this.expiredAlertOn,
    required this.newitemAlertOn,
    required this.envAlertOn,
    required this.doorAlertOn,
  });

  static const melodyNames = ['Beep-beep', 'Single', 'Fast', 'Siren'];

  /// Firmware NVS defaults — used until the RTDB copy loads.
  static const defaults = FridgeSettings(
    tempMin: 2,
    tempMax: 8,
    humMin: 30,
    humMax: 70,
    buzzerEnabled: true,
    doorAlertS: 30,
    buzzerVolume: 80,
    buzzerFreq: 2000,
    buzzerDurationS: 10,
    buzzerMelody: 0,
    notifRetentionD: 30,
    expiryWarnDays: 3,
    expiryAlertOn: true,
    expiredAlertOn: true,
    newitemAlertOn: true,
    envAlertOn: true,
    doorAlertOn: true,
  );

  static int _int(dynamic v, int d) =>
      v is num ? v.toInt() : (v is String ? int.tryParse(v) ?? d : d);
  static bool _bool(dynamic v, bool d) => v is bool ? v : d;

  factory FridgeSettings.fromMap(Map<dynamic, dynamic> m) {
    const d = defaults;
    return FridgeSettings(
      tempMin: _int(m['temp_min'], d.tempMin),
      tempMax: _int(m['temp_max'], d.tempMax),
      humMin: _int(m['hum_min'], d.humMin),
      humMax: _int(m['hum_max'], d.humMax),
      buzzerEnabled: _bool(m['buzzer_enabled'], d.buzzerEnabled),
      doorAlertS: _int(m['door_alert_s'], d.doorAlertS),
      buzzerVolume: _int(m['buzzer_volume'], d.buzzerVolume),
      buzzerFreq: _int(m['buzzer_freq'], d.buzzerFreq),
      buzzerDurationS: _int(m['buzzer_duration_s'], d.buzzerDurationS),
      buzzerMelody: _int(m['buzzer_melody'], d.buzzerMelody),
      notifRetentionD: _int(m['notif_retention_d'], d.notifRetentionD),
      expiryWarnDays: _int(m['expiry_warn_days'], d.expiryWarnDays),
      expiryAlertOn: _bool(m['expiry_alert_on'], d.expiryAlertOn),
      expiredAlertOn: _bool(m['expired_alert_on'], d.expiredAlertOn),
      newitemAlertOn: _bool(m['newitem_alert_on'], d.newitemAlertOn),
      envAlertOn: _bool(m['env_alert_on'], d.envAlertOn),
      doorAlertOn: _bool(m['door_alert_on'], d.doorAlertOn),
    );
  }

  /// The settings fields only (the service adds updated_by/updated_at).
  /// Field names must match the firmware's buildSettingsJson() exactly.
  Map<String, dynamic> toMap() => {
        'temp_min': tempMin,
        'temp_max': tempMax,
        'hum_min': humMin,
        'hum_max': humMax,
        'buzzer_enabled': buzzerEnabled,
        'door_alert_s': doorAlertS,
        'buzzer_volume': buzzerVolume,
        'buzzer_freq': buzzerFreq,
        'buzzer_duration_s': buzzerDurationS,
        'buzzer_melody': buzzerMelody,
        'notif_retention_d': notifRetentionD,
        'expiry_warn_days': expiryWarnDays,
        'expiry_alert_on': expiryAlertOn,
        'expired_alert_on': expiredAlertOn,
        'newitem_alert_on': newitemAlertOn,
        'env_alert_on': envAlertOn,
        'door_alert_on': doorAlertOn,
      };

  String get melodyName =>
      (buzzerMelody >= 0 && buzzerMelody < melodyNames.length)
          ? melodyNames[buzzerMelody]
          : melodyNames[0];

  FridgeSettings copyWith({
    int? tempMin,
    int? tempMax,
    int? humMin,
    int? humMax,
    bool? buzzerEnabled,
    int? doorAlertS,
    int? buzzerVolume,
    int? buzzerFreq,
    int? buzzerDurationS,
    int? buzzerMelody,
    int? notifRetentionD,
    int? expiryWarnDays,
    bool? expiryAlertOn,
    bool? expiredAlertOn,
    bool? newitemAlertOn,
    bool? envAlertOn,
    bool? doorAlertOn,
  }) =>
      FridgeSettings(
        tempMin: tempMin ?? this.tempMin,
        tempMax: tempMax ?? this.tempMax,
        humMin: humMin ?? this.humMin,
        humMax: humMax ?? this.humMax,
        buzzerEnabled: buzzerEnabled ?? this.buzzerEnabled,
        doorAlertS: doorAlertS ?? this.doorAlertS,
        buzzerVolume: buzzerVolume ?? this.buzzerVolume,
        buzzerFreq: buzzerFreq ?? this.buzzerFreq,
        buzzerDurationS: buzzerDurationS ?? this.buzzerDurationS,
        buzzerMelody: buzzerMelody ?? this.buzzerMelody,
        notifRetentionD: notifRetentionD ?? this.notifRetentionD,
        expiryWarnDays: expiryWarnDays ?? this.expiryWarnDays,
        expiryAlertOn: expiryAlertOn ?? this.expiryAlertOn,
        expiredAlertOn: expiredAlertOn ?? this.expiredAlertOn,
        newitemAlertOn: newitemAlertOn ?? this.newitemAlertOn,
        envAlertOn: envAlertOn ?? this.envAlertOn,
        doorAlertOn: doorAlertOn ?? this.doorAlertOn,
      );
}
