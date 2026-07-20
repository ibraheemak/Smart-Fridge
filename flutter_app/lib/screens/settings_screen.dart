import 'package:flutter/material.dart';
import '../services/auth_service.dart';
import '../services/fridge_service.dart';
import '../services/icon_generator_service.dart';
import '../services/role_service.dart';
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
  late double _minTemp;
  late double _maxTemp;
  late int _expiryDays;

  @override
  void initState() {
    super.initState();
    _minTemp = SettingsService.getMinTemp();
    _maxTemp = SettingsService.getMaxTemp();
    _expiryDays = SettingsService.getExpiryWarningDays();
  }

  @override
  Widget build(BuildContext context) {
    final user = AuthService.currentUser;
    final name = AuthService.displayName;
    // Alert thresholds and icon regeneration are Homeowner-only.
    final isAdmin = RoleService.isAdmin;

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

                  // Alert preferences — Homeowner-only; drive the alert logic
                  _SettingsSection(
                    title: 'Alert Preferences',
                    icon: Icons.notifications_active_rounded,
                    iconColor: AppColors.primary,
                    children: [
                      if (!isAdmin)
                        const Padding(
                          padding: EdgeInsets.fromLTRB(16, 10, 16, 0),
                          child: Row(
                            children: [
                              Icon(Icons.lock_outline_rounded,
                                  size: 14,
                                  color: AppColors.onSurfaceVariant),
                              SizedBox(width: 8),
                              Expanded(
                                child: Text(
                                    'Only the Homeowner can change these.',
                                    style: TextStyle(
                                        fontSize: 11,
                                        color: AppColors.onSurfaceVariant)),
                              ),
                            ],
                          ),
                        ),
                      Padding(
                        padding: const EdgeInsets.fromLTRB(16, 12, 16, 0),
                        child: Row(
                          children: [
                            const Expanded(
                              child: Column(
                                crossAxisAlignment: CrossAxisAlignment.start,
                                children: [
                                  Text('Safe Temperature Range',
                                      style: TextStyle(
                                          fontSize: 14,
                                          fontWeight: FontWeight.w600)),
                                  Text(
                                      'Outside this range the app shows a temperature alert',
                                      style: TextStyle(
                                          fontSize: 11,
                                          color: AppColors.onSurfaceVariant)),
                                ],
                              ),
                            ),
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
                          onChanged: isAdmin
                              ? (r) {
                                  setState(() {
                                    _minTemp = r.start;
                                    _maxTemp = r.end;
                                  });
                                  SettingsService.setMinTemp(r.start);
                                  SettingsService.setMaxTemp(r.end);
                                }
                              : null,
                        ),
                      ),
                      Padding(
                        padding: const EdgeInsets.fromLTRB(16, 4, 16, 12),
                        child: Row(
                          children: [
                            const Expanded(
                              child: Column(
                                crossAxisAlignment: CrossAxisAlignment.start,
                                children: [
                                  Text('Expiry Advance Warning',
                                      style: TextStyle(
                                          fontSize: 14,
                                          fontWeight: FontWeight.w600)),
                                  Text(
                                      'Items expiring within this many days are marked critical',
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
                                  onTap: isAdmin && _expiryDays > 1
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
                                  onTap: isAdmin && _expiryDays < 7
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
                      if (isAdmin)
                        const Padding(
                          padding: EdgeInsets.fromLTRB(16, 0, 16, 8),
                          child: _RegenerateIconsButton(),
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
        icon: Icons.person_rounded,
        label: 'Account',
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
        Text('Hello, $name',
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

class _RegenerateIconsButton extends StatefulWidget {
  const _RegenerateIconsButton();

  @override
  State<_RegenerateIconsButton> createState() =>
      _RegenerateIconsButtonState();
}

class _RegenerateIconsButtonState extends State<_RegenerateIconsButton> {
  bool _running = false;
  String _status = '';

  Future<void> _run() async {
    setState(() {
      _running = true;
      _status = 'Loading inventory…';
    });

    try {
      // Fetch current item names from Firestore (one-shot)
      final snap = await FridgeService.inventoryStream().first;
      final names = snap?.items.map((i) => i.name).toList() ?? [];
      if (!mounted) return;

      if (names.isEmpty) {
        setState(() {
          _status = 'No items in fridge.';
          _running = false;
        });
        return;
      }

      setState(() => _status = 'Generating ${names.length} icons…');
      await IconGeneratorService.regenerateAll(names);

      if (mounted) {
        setState(() {
          _status = '${names.length} icons updated!';
          _running = false;
        });
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(
            content: Text('All icons regenerated with consistent style'),
            backgroundColor: AppColors.secondary,
          ),
        );
      }
    } catch (_) {
      if (mounted) {
        setState(() {
          _status = 'Failed — check connection.';
          _running = false;
        });
      }
    }
  }

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      width: double.infinity,
      child: OutlinedButton.icon(
        onPressed: _running ? null : _run,
        icon: _running
            ? const SizedBox(
                width: 16,
                height: 16,
                child: CircularProgressIndicator(
                    strokeWidth: 2, color: AppColors.primary))
            : const Icon(Icons.auto_fix_high_rounded,
                color: AppColors.primary),
        label: Text(
          _running
              ? _status
              : 'Regenerate All Icons',
          style: const TextStyle(color: AppColors.primary),
        ),
        style: OutlinedButton.styleFrom(
          side: const BorderSide(color: AppColors.primary),
          shape:
              RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
          padding: const EdgeInsets.symmetric(vertical: 14),
        ),
      ),
    );
  }
}
