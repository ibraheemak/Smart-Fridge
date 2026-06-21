import 'package:flutter/material.dart';
import '../services/auth_service.dart';
import '../services/settings_service.dart';
import '../theme/app_theme.dart';
import 'analytics_screen.dart';
import 'temperature_screen.dart';
import 'users_screen.dart';
import 'camera_screen.dart';

class SettingsScreen extends StatefulWidget {
  const SettingsScreen({super.key});

  @override
  State<SettingsScreen> createState() => _SettingsScreenState();
}

class _SettingsScreenState extends State<SettingsScreen> {
  // Door
  late double _doorTimer;
  late bool _buzzer;
  late bool _mobileAlerts;
  // Temperature
  late double _minTemp;
  late double _maxTemp;
  late bool _tempAlerts;
  // Camera
  late double _scanDelay;
  late bool _autoScan;
  late bool _flashOnScan;
  // Expiry
  late int _expiryDays;
  late bool _dailySummary;

  @override
  void initState() {
    super.initState();
    _doorTimer = SettingsService.getDoorOpenTimer();
    _buzzer = SettingsService.getBuzzerEnabled();
    _mobileAlerts = SettingsService.getMobileAlerts();
    _minTemp = SettingsService.getMinTemp();
    _maxTemp = SettingsService.getMaxTemp();
    _tempAlerts = SettingsService.getTempAlerts();
    _scanDelay = SettingsService.getScanDelay();
    _autoScan = SettingsService.getAutoScan();
    _flashOnScan = SettingsService.getFlashOnScan();
    _expiryDays = SettingsService.getExpiryWarningDays();
    _dailySummary = SettingsService.getDailySummary();
  }

  @override
  Widget build(BuildContext context) {
    final user = AuthService.currentUser;
    final name = AuthService.displayName;

    return Scaffold(
      backgroundColor: AppColors.background,
      body: CustomScrollView(
        slivers: [
          // App Bar
          SliverAppBar(
            floating: true,
            backgroundColor: AppColors.background,
            elevation: 0,
            scrolledUnderElevation: 0,
            titleSpacing: 16,
            title: Text('Settings',
                style: Theme.of(context).textTheme.headlineMedium),
          ),

          SliverToBoxAdapter(
            child: Padding(
              padding: const EdgeInsets.fromLTRB(16, 0, 16, 100),
              child: Column(
                children: [
                  // Quick links
                  _QuickLinks(name: name),

                  const SizedBox(height: 20),

                  // Door Settings
                  _SettingsSection(
                    title: 'Door Settings',
                    icon: Icons.door_front_door_rounded,
                    iconColor: AppColors.primary,
                    children: [
                      _SliderTile(
                        label: 'Open Alert Timer',
                        value: _doorTimer,
                        min: 10,
                        max: 120,
                        divisions: 11,
                        format: (v) => '${v.round()}s',
                        onChanged: (v) {
                          setState(() => _doorTimer = v);
                          SettingsService.setDoorOpenTimer(v);
                        },
                      ),
                      _SwitchTile(
                        label: 'Buzzer Alert',
                        subtitle: 'Sound alert when door is left open',
                        value: _buzzer,
                        onChanged: (v) {
                          setState(() => _buzzer = v);
                          SettingsService.setBuzzerEnabled(v);
                        },
                      ),
                      _SwitchTile(
                        label: 'Mobile Notifications',
                        subtitle: 'Push alert when door is open too long',
                        value: _mobileAlerts,
                        onChanged: (v) {
                          setState(() => _mobileAlerts = v);
                          SettingsService.setMobileAlerts(v);
                        },
                      ),
                    ],
                  ),

                  const SizedBox(height: 16),

                  // Temperature Settings
                  _SettingsSection(
                    title: 'Temperature Settings',
                    icon: Icons.thermostat_rounded,
                    iconColor: const Color(0xFF0288D1),
                    children: [
                      Padding(
                        padding: const EdgeInsets.fromLTRB(16, 12, 16, 0),
                        child: Row(
                          children: [
                            const Text('Desired Range',
                                style: TextStyle(
                                    fontSize: 14,
                                    fontWeight: FontWeight.w600)),
                            const Spacer(),
                            Text(
                                '${_minTemp.round()}°C – ${_maxTemp.round()}°C',
                                style: const TextStyle(
                                    fontSize: 13,
                                    fontWeight: FontWeight.w700,
                                    color: AppColors.primary)),
                          ],
                        ),
                      ),
                      Padding(
                        padding: const EdgeInsets.symmetric(horizontal: 8),
                        child: RangeSlider(
                          values: RangeValues(_minTemp, _maxTemp),
                          min: 0,
                          max: 10,
                          divisions: 10,
                          activeColor: AppColors.primary,
                          inactiveColor: AppColors.outlineVariant,
                          onChanged: (r) {
                            setState(() {
                              _minTemp = r.start;
                              _maxTemp = r.end;
                            });
                            SettingsService.setMinTemp(r.start);
                            SettingsService.setMaxTemp(r.end);
                          },
                        ),
                      ),
                      _SwitchTile(
                        label: 'Deviation Alerts',
                        subtitle: 'Alert when temperature goes out of range',
                        value: _tempAlerts,
                        onChanged: (v) {
                          setState(() => _tempAlerts = v);
                          SettingsService.setTempAlerts(v);
                        },
                      ),
                    ],
                  ),

                  const SizedBox(height: 16),

                  // Camera Settings
                  _SettingsSection(
                    title: 'Camera Settings',
                    icon: Icons.camera_alt_rounded,
                    iconColor: AppColors.secondary,
                    children: [
                      _SliderTile(
                        label: 'Auto Scan Interval',
                        value: _scanDelay,
                        min: 5,
                        max: 60,
                        divisions: 11,
                        format: (v) => '${v.round()} min',
                        onChanged: (v) {
                          setState(() => _scanDelay = v);
                          SettingsService.setScanDelay(v);
                        },
                      ),
                      _SwitchTile(
                        label: 'Auto Scan on Door Close',
                        subtitle: 'Automatically scan when door closes',
                        value: _autoScan,
                        onChanged: (v) {
                          setState(() => _autoScan = v);
                          SettingsService.setAutoScan(v);
                        },
                      ),
                      _SwitchTile(
                        label: 'Flash During Scan',
                        subtitle: 'Use LED strip for better image quality',
                        value: _flashOnScan,
                        onChanged: (v) {
                          setState(() => _flashOnScan = v);
                          SettingsService.setFlashOnScan(v);
                        },
                      ),
                    ],
                  ),

                  const SizedBox(height: 16),

                  // Expiry Settings
                  _SettingsSection(
                    title: 'Expiry Settings',
                    icon: Icons.event_rounded,
                    iconColor: AppColors.tertiaryFixedDim,
                    children: [
                      Padding(
                        padding: const EdgeInsets.fromLTRB(16, 12, 16, 0),
                        child: Row(
                          children: [
                            const Expanded(
                              child: Column(
                                crossAxisAlignment: CrossAxisAlignment.start,
                                children: [
                                  Text('Advance Warning',
                                      style: TextStyle(
                                          fontSize: 14,
                                          fontWeight: FontWeight.w600)),
                                  Text('Alert before expiry date',
                                      style: TextStyle(
                                          fontSize: 11,
                                          color: AppColors.onSurfaceVariant)),
                                ],
                              ),
                            ),
                            Row(
                              children: [
                                _StepButton(
                                  icon: Icons.remove,
                                  onTap: _expiryDays > 1
                                      ? () {
                                          setState(() => _expiryDays--);
                                          SettingsService
                                              .setExpiryWarningDays(_expiryDays);
                                        }
                                      : null,
                                ),
                                Padding(
                                  padding:
                                      const EdgeInsets.symmetric(horizontal: 12),
                                  child: Text('$_expiryDays days',
                                      style: const TextStyle(
                                          fontSize: 15,
                                          fontWeight: FontWeight.w700)),
                                ),
                                _StepButton(
                                  icon: Icons.add,
                                  onTap: _expiryDays < 14
                                      ? () {
                                          setState(() => _expiryDays++);
                                          SettingsService
                                              .setExpiryWarningDays(_expiryDays);
                                        }
                                      : null,
                                ),
                              ],
                            ),
                          ],
                        ),
                      ),
                      _SwitchTile(
                        label: 'Daily Expiry Summary',
                        subtitle: 'Morning notification with today\'s expirations',
                        value: _dailySummary,
                        onChanged: (v) {
                          setState(() => _dailySummary = v);
                          SettingsService.setDailySummary(v);
                        },
                      ),
                    ],
                  ),

                  const SizedBox(height: 16),

                  // Account
                  _SettingsSection(
                    title: 'Account',
                    icon: Icons.person_rounded,
                    iconColor: AppColors.primary,
                    children: [
                      Padding(
                        padding: const EdgeInsets.fromLTRB(16, 12, 16, 12),
                        child: Row(
                          children: [
                            CircleAvatar(
                              radius: 24,
                              backgroundColor: AppColors.primaryFixed,
                              child: Text(
                                name.isNotEmpty ? name[0].toUpperCase() : 'U',
                                style: const TextStyle(
                                    fontSize: 18,
                                    fontWeight: FontWeight.w700,
                                    color: AppColors.primary),
                              ),
                            ),
                            const SizedBox(width: 14),
                            Expanded(
                              child: Column(
                                crossAxisAlignment: CrossAxisAlignment.start,
                                children: [
                                  Text(name,
                                      style: const TextStyle(
                                          fontSize: 15,
                                          fontWeight: FontWeight.w700)),
                                  Text(
                                    user?.email ?? 'Not signed in',
                                    style: const TextStyle(
                                        fontSize: 12,
                                        color: AppColors.onSurfaceVariant),
                                  ),
                                ],
                              ),
                            ),
                          ],
                        ),
                      ),
                      Padding(
                        padding: const EdgeInsets.fromLTRB(16, 0, 16, 16),
                        child: SizedBox(
                          width: double.infinity,
                          child: OutlinedButton.icon(
                            onPressed: () async {
                              await AuthService.signOut();
                            },
                            icon: const Icon(Icons.logout_rounded,
                                color: AppColors.error),
                            label: const Text('Sign Out',
                                style: TextStyle(color: AppColors.error)),
                            style: OutlinedButton.styleFrom(
                              side: const BorderSide(color: AppColors.error),
                              shape: RoundedRectangleBorder(
                                  borderRadius: BorderRadius.circular(12)),
                              padding:
                                  const EdgeInsets.symmetric(vertical: 14),
                            ),
                          ),
                        ),
                      ),
                    ],
                  ),
                ],
              ),
            ),
          ),
        ],
      ),
    );
  }
}

class _QuickLinks extends StatelessWidget {
  final String name;

  const _QuickLinks({required this.name});

  @override
  Widget build(BuildContext context) {
    final links = [
      _QuickLink(
        icon: Icons.bar_chart_rounded,
        label: 'Analytics',
        color: AppColors.primary,
        onTap: () => Navigator.push(
            context,
            MaterialPageRoute(
                builder: (_) => const AnalyticsScreen())),
      ),
      _QuickLink(
        icon: Icons.thermostat_rounded,
        label: 'Temperature',
        color: const Color(0xFF0288D1),
        onTap: () => Navigator.push(
            context,
            MaterialPageRoute(
                builder: (_) => const TemperatureScreen())),
      ),
      _QuickLink(
        icon: Icons.people_rounded,
        label: 'Users',
        color: AppColors.secondary,
        onTap: () => Navigator.push(
            context,
            MaterialPageRoute(
                builder: (_) => const UsersScreen())),
      ),
      _QuickLink(
        icon: Icons.videocam_rounded,
        label: 'Live View',
        color: AppColors.tertiaryFixedDim,
        onTap: () => Navigator.push(
            context,
            MaterialPageRoute(
                builder: (_) => const CameraScreen())),
      ),
    ];

    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text('Hello, $name 👋',
            style: Theme.of(context)
                .textTheme
                .titleLarge
                ?.copyWith(fontWeight: FontWeight.w700)),
        const SizedBox(height: 4),
        Text('Smart Fridge — Group #8',
            style: Theme.of(context).textTheme.bodyMedium),
        const SizedBox(height: 16),
        Row(
          children: links.map((l) => Expanded(child: l)).toList(),
        ),
      ],
    );
  }
}

class _QuickLink extends StatelessWidget {
  final IconData icon;
  final String label;
  final Color color;
  final VoidCallback onTap;

  const _QuickLink(
      {required this.icon,
      required this.label,
      required this.color,
      required this.onTap});

  @override
  Widget build(BuildContext context) {
    return GestureDetector(
      onTap: onTap,
      child: Container(
        margin: const EdgeInsets.only(right: 10),
        padding: const EdgeInsets.symmetric(vertical: 14),
        decoration: BoxDecoration(
          color: color.withValues(alpha: 0.1),
          borderRadius: BorderRadius.circular(14),
          border: Border.all(color: color.withValues(alpha: 0.2)),
        ),
        child: Column(
          children: [
            Icon(icon, size: 24, color: color),
            const SizedBox(height: 6),
            Text(label,
                style: TextStyle(
                    fontSize: 11,
                    fontWeight: FontWeight.w700,
                    color: color),
                textAlign: TextAlign.center),
          ],
        ),
      ),
    );
  }
}

class _SettingsSection extends StatelessWidget {
  final String title;
  final IconData icon;
  final Color iconColor;
  final List<Widget> children;

  const _SettingsSection({
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
          BoxShadow(
              color: Color(0x0A000000),
              blurRadius: 8,
              offset: Offset(0, 2)),
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
              height: 1,
              indent: 16,
              endIndent: 16,
              color: AppColors.outlineVariant),
          ...children,
        ],
      ),
    );
  }
}

class _SliderTile extends StatelessWidget {
  final String label;
  final double value;
  final double min;
  final double max;
  final int divisions;
  final String Function(double) format;
  final ValueChanged<double> onChanged;

  const _SliderTile({
    required this.label,
    required this.value,
    required this.min,
    required this.max,
    required this.divisions,
    required this.format,
    required this.onChanged,
  });

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.fromLTRB(16, 12, 16, 4),
      child: Column(
        children: [
          Row(
            children: [
              Text(label,
                  style: const TextStyle(
                      fontSize: 14, fontWeight: FontWeight.w600)),
              const Spacer(),
              Text(format(value),
                  style: const TextStyle(
                      fontSize: 13,
                      fontWeight: FontWeight.w700,
                      color: AppColors.primary)),
            ],
          ),
          Slider(
            value: value,
            min: min,
            max: max,
            divisions: divisions,
            activeColor: AppColors.primary,
            inactiveColor: AppColors.outlineVariant,
            onChanged: onChanged,
          ),
        ],
      ),
    );
  }
}

class _SwitchTile extends StatelessWidget {
  final String label;
  final String subtitle;
  final bool value;
  final ValueChanged<bool> onChanged;

  const _SwitchTile({
    required this.label,
    required this.subtitle,
    required this.value,
    required this.onChanged,
  });

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 10),
      child: Row(
        children: [
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(label,
                    style: const TextStyle(
                        fontSize: 14, fontWeight: FontWeight.w600)),
                Text(subtitle,
                    style: const TextStyle(
                        fontSize: 11, color: AppColors.onSurfaceVariant)),
              ],
            ),
          ),
          Switch(
            value: value,
            activeColor: AppColors.primary,
            onChanged: onChanged,
          ),
        ],
      ),
    );
  }
}

class _StepButton extends StatelessWidget {
  final IconData icon;
  final VoidCallback? onTap;

  const _StepButton({required this.icon, this.onTap});

  @override
  Widget build(BuildContext context) {
    return GestureDetector(
      onTap: onTap,
      child: Container(
        width: 32,
        height: 32,
        decoration: BoxDecoration(
          color: onTap != null
              ? AppColors.primaryFixed
              : AppColors.surfaceContainerHighest,
          shape: BoxShape.circle,
        ),
        child: Icon(icon,
            size: 16,
            color: onTap != null
                ? AppColors.primary
                : AppColors.outline),
      ),
    );
  }
}
