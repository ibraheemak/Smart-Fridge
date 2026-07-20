import 'dart:async';
import 'package:flutter/material.dart';
import '../models/fridge_settings.dart';
import '../services/fridge_settings_service.dart';
import '../theme/app_theme.dart';

/// Manager-only screen mirroring the ESP32 CH screen's settings. Edits sync
/// to the fridge screen in real time (RTDB), and changes made on the fridge
/// screen appear here live.
class FridgeSettingsScreen extends StatefulWidget {
  const FridgeSettingsScreen({super.key});

  @override
  State<FridgeSettingsScreen> createState() => _FridgeSettingsScreenState();
}

class _FridgeSettingsScreenState extends State<FridgeSettingsScreen> {
  FridgeSettings? _s;
  StreamSubscription<FridgeSettings?>? _sub;
  Timer? _writeTimer;
  bool _localDirty = false; // suppress adopting remote while the user edits

  @override
  void initState() {
    super.initState();
    _s = FridgeSettingsService.cached;
    _sub = FridgeSettingsService.stream().listen((remote) {
      // Don't clobber an in-progress local edit; adopt screen-side changes
      // (and our own echoes) once settled.
      if (remote != null && !_localDirty && mounted) {
        setState(() => _s = remote);
      }
    });
  }

  @override
  void dispose() {
    _sub?.cancel();
    _writeTimer?.cancel();
    super.dispose();
  }

  // Optimistic local update + debounced write to RTDB (tagged updated_by:app).
  void _edit(FridgeSettings next) {
    setState(() {
      _s = next;
      _localDirty = true;
    });
    _writeTimer?.cancel();
    _writeTimer = Timer(const Duration(milliseconds: 450), () async {
      final snapshot = _s;
      if (snapshot != null) await FridgeSettingsService.write(snapshot);
      _localDirty = false;
    });
  }

  @override
  Widget build(BuildContext context) {
    final s = _s ?? FridgeSettings.defaults;
    return Scaffold(
      backgroundColor: AppColors.background,
      appBar: AppBar(
        backgroundColor: AppColors.background,
        elevation: 0,
        scrolledUnderElevation: 0,
        leading: IconButton(
          icon: const Icon(Icons.arrow_back_rounded, color: AppColors.onSurface),
          onPressed: () => Navigator.pop(context),
        ),
        title: const Text('Fridge Settings',
            style: TextStyle(
                color: AppColors.onSurface,
                fontSize: 20,
                fontWeight: FontWeight.w700)),
      ),
      body: ListView(
        padding: const EdgeInsets.fromLTRB(16, 8, 16, 32),
        children: [
          Container(
            padding: const EdgeInsets.all(12),
            decoration: BoxDecoration(
              color: AppColors.primaryFixed,
              borderRadius: BorderRadius.circular(12),
            ),
            child: const Row(
              children: [
                Icon(Icons.sync_rounded, size: 18, color: AppColors.primary),
                SizedBox(width: 10),
                Expanded(
                  child: Text(
                    'These control the fridge screen too. Changes sync both ways in real time.',
                    style: TextStyle(fontSize: 12.5, color: AppColors.primary),
                  ),
                ),
              ],
            ),
          ),
          const SizedBox(height: 16),

          // ── Temperature & Humidity ────────────────────────────────────
          _Section(
            title: 'Safe Ranges',
            icon: Icons.thermostat_rounded,
            iconColor: const Color(0xFF0288D1),
            children: [
              _Stepper(
                label: 'Min temperature',
                value: '${s.tempMin}°C',
                onMinus: s.tempMin > -20
                    ? () => _edit(s.copyWith(tempMin: s.tempMin - 1))
                    : null,
                onPlus: s.tempMin < s.tempMax - 1
                    ? () => _edit(s.copyWith(tempMin: s.tempMin + 1))
                    : null,
              ),
              _Stepper(
                label: 'Max temperature',
                value: '${s.tempMax}°C',
                onMinus: s.tempMax > s.tempMin + 1
                    ? () => _edit(s.copyWith(tempMax: s.tempMax - 1))
                    : null,
                onPlus: s.tempMax < 30
                    ? () => _edit(s.copyWith(tempMax: s.tempMax + 1))
                    : null,
              ),
              _Stepper(
                label: 'Min humidity',
                value: '${s.humMin}%',
                onMinus: s.humMin > 0
                    ? () => _edit(s.copyWith(humMin: s.humMin - 5))
                    : null,
                onPlus: s.humMin < s.humMax - 5
                    ? () => _edit(s.copyWith(humMin: s.humMin + 5))
                    : null,
              ),
              _Stepper(
                label: 'Max humidity',
                value: '${s.humMax}%',
                onMinus: s.humMax > s.humMin + 5
                    ? () => _edit(s.copyWith(humMax: s.humMax - 5))
                    : null,
                onPlus: s.humMax < 100
                    ? () => _edit(s.copyWith(humMax: s.humMax + 5))
                    : null,
              ),
            ],
          ),
          const SizedBox(height: 16),

          // ── Alerts ────────────────────────────────────────────────────
          _Section(
            title: 'Alerts',
            icon: Icons.notifications_active_rounded,
            iconColor: AppColors.primary,
            children: [
              _Toggle(
                label: 'Expiring-soon alerts',
                value: s.expiryAlertOn,
                onChanged: (v) => _edit(s.copyWith(expiryAlertOn: v)),
              ),
              _Stepper(
                label: 'Warn days before expiry',
                value: '${s.expiryWarnDays} d',
                onMinus: s.expiryWarnDays > 0
                    ? () => _edit(s.copyWith(expiryWarnDays: s.expiryWarnDays - 1))
                    : null,
                onPlus: s.expiryWarnDays < 30
                    ? () => _edit(s.copyWith(expiryWarnDays: s.expiryWarnDays + 1))
                    : null,
              ),
              _Toggle(
                label: 'Expired-item alerts',
                value: s.expiredAlertOn,
                onChanged: (v) => _edit(s.copyWith(expiredAlertOn: v)),
              ),
              _Toggle(
                label: 'New-item alerts',
                value: s.newitemAlertOn,
                onChanged: (v) => _edit(s.copyWith(newitemAlertOn: v)),
              ),
              _Toggle(
                label: 'Temperature / humidity alerts',
                value: s.envAlertOn,
                onChanged: (v) => _edit(s.copyWith(envAlertOn: v)),
              ),
              _Toggle(
                label: 'Door-open alerts',
                value: s.doorAlertOn,
                onChanged: (v) => _edit(s.copyWith(doorAlertOn: v)),
              ),
              _Stepper(
                label: 'Keep notifications for',
                value: '${s.notifRetentionD} d',
                onMinus: s.notifRetentionD > 1
                    ? () => _edit(s.copyWith(notifRetentionD: s.notifRetentionD - 1))
                    : null,
                onPlus: s.notifRetentionD < 90
                    ? () => _edit(s.copyWith(notifRetentionD: s.notifRetentionD + 1))
                    : null,
              ),
            ],
          ),
          const SizedBox(height: 16),

          // ── Door & Buzzer ─────────────────────────────────────────────
          _Section(
            title: 'Buzzer',
            icon: Icons.volume_up_rounded,
            iconColor: AppColors.secondary,
            children: [
              _Toggle(
                label: 'Buzzer enabled',
                value: s.buzzerEnabled,
                onChanged: (v) => _edit(s.copyWith(buzzerEnabled: v)),
              ),
              _Stepper(
                label: 'Door-open buzz delay',
                value: '${s.doorAlertS}s',
                onMinus: s.doorAlertS > 5
                    ? () => _edit(s.copyWith(doorAlertS: s.doorAlertS - 5))
                    : null,
                onPlus: s.doorAlertS < 300
                    ? () => _edit(s.copyWith(doorAlertS: s.doorAlertS + 5))
                    : null,
              ),
              _Stepper(
                label: 'Volume',
                value: '${s.buzzerVolume}%',
                onMinus: s.buzzerVolume > 0
                    ? () => _edit(s.copyWith(buzzerVolume: s.buzzerVolume - 10))
                    : null,
                onPlus: s.buzzerVolume < 100
                    ? () => _edit(s.copyWith(buzzerVolume: s.buzzerVolume + 10))
                    : null,
              ),
              _Stepper(
                label: 'Tone',
                value: '${s.buzzerFreq} Hz',
                onMinus: s.buzzerFreq > 500
                    ? () => _edit(s.copyWith(buzzerFreq: s.buzzerFreq - 100))
                    : null,
                onPlus: s.buzzerFreq < 4000
                    ? () => _edit(s.copyWith(buzzerFreq: s.buzzerFreq + 100))
                    : null,
              ),
              _Stepper(
                label: 'Buzz duration',
                value: '${s.buzzerDurationS}s',
                onMinus: s.buzzerDurationS > 2
                    ? () => _edit(s.copyWith(buzzerDurationS: s.buzzerDurationS - 2))
                    : null,
                onPlus: s.buzzerDurationS < 60
                    ? () => _edit(s.copyWith(buzzerDurationS: s.buzzerDurationS + 2))
                    : null,
              ),
              _Stepper(
                label: 'Melody',
                value: s.melodyName,
                onMinus: () => _edit(s.copyWith(
                    buzzerMelody: (s.buzzerMelody + FridgeSettings.melodyNames.length - 1) %
                        FridgeSettings.melodyNames.length)),
                onPlus: () => _edit(s.copyWith(
                    buzzerMelody: (s.buzzerMelody + 1) %
                        FridgeSettings.melodyNames.length)),
              ),
            ],
          ),
        ],
      ),
    );
  }
}

// ── Reusable rows ─────────────────────────────────────────────────────────────
class _Section extends StatelessWidget {
  final String title;
  final IconData icon;
  final Color iconColor;
  final List<Widget> children;

  const _Section({
    required this.title,
    required this.icon,
    required this.iconColor,
    required this.children,
  });

  @override
  Widget build(BuildContext context) {
    return Container(
      decoration: BoxDecoration(
        color: AppColors.surfaceContainerLowest,
        borderRadius: BorderRadius.circular(20),
        boxShadow: const [
          BoxShadow(color: Color(0x0A000000), blurRadius: 8, offset: Offset(0, 2)),
        ],
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Padding(
            padding: const EdgeInsets.fromLTRB(16, 16, 16, 4),
            child: Row(
              children: [
                Icon(icon, size: 18, color: iconColor),
                const SizedBox(width: 8),
                Text(title,
                    style: const TextStyle(
                        fontSize: 14,
                        fontWeight: FontWeight.w700,
                        color: AppColors.onSurface)),
              ],
            ),
          ),
          const Divider(
              height: 1, indent: 16, endIndent: 16, color: AppColors.outlineVariant),
          ...children,
        ],
      ),
    );
  }
}

class _Stepper extends StatelessWidget {
  final String label;
  final String value;
  final VoidCallback? onMinus;
  final VoidCallback? onPlus;

  const _Stepper({
    required this.label,
    required this.value,
    required this.onMinus,
    required this.onPlus,
  });

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
      child: Row(
        children: [
          Expanded(
            child: Text(label,
                style: const TextStyle(fontSize: 14, fontWeight: FontWeight.w600)),
          ),
          _RoundBtn(icon: Icons.remove, onTap: onMinus),
          Container(
            width: 78,
            alignment: Alignment.center,
            child: Text(value,
                style: const TextStyle(
                    fontSize: 14,
                    fontWeight: FontWeight.w700,
                    color: AppColors.primary)),
          ),
          _RoundBtn(icon: Icons.add, onTap: onPlus),
        ],
      ),
    );
  }
}

class _RoundBtn extends StatelessWidget {
  final IconData icon;
  final VoidCallback? onTap;

  const _RoundBtn({required this.icon, required this.onTap});

  @override
  Widget build(BuildContext context) {
    final enabled = onTap != null;
    return GestureDetector(
      onTap: onTap,
      child: Container(
        width: 34,
        height: 34,
        decoration: BoxDecoration(
          color: enabled
              ? AppColors.primaryFixed
              : AppColors.surfaceContainerHighest,
          shape: BoxShape.circle,
        ),
        child: Icon(icon,
            size: 18, color: enabled ? AppColors.primary : AppColors.outline),
      ),
    );
  }
}

class _Toggle extends StatelessWidget {
  final String label;
  final bool value;
  final ValueChanged<bool> onChanged;

  const _Toggle({
    required this.label,
    required this.value,
    required this.onChanged,
  });

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 2),
      child: Row(
        children: [
          Expanded(
            child: Text(label,
                style: const TextStyle(fontSize: 14, fontWeight: FontWeight.w600)),
          ),
          Switch(
            value: value,
            thumbColor: WidgetStateProperty.resolveWith((st) =>
                st.contains(WidgetState.selected) ? AppColors.primary : null),
            onChanged: onChanged,
          ),
        ],
      ),
    );
  }
}
