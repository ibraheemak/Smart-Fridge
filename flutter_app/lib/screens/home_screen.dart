import 'dart:typed_data';
import 'package:flutter/material.dart';
import 'package:http/http.dart' as http;
import '../config.dart';
import '../models/fridge_item.dart';
import '../models/scan_record.dart';
import '../services/auth_service.dart';
import '../services/fridge_service.dart';
import '../theme/app_theme.dart';
import 'camera_screen.dart';
import 'expiry_screen.dart';
import 'temperature_screen.dart';

class HomeScreen extends StatefulWidget {
  final void Function(int index) onNavigate;

  const HomeScreen({super.key, required this.onNavigate});

  @override
  State<HomeScreen> createState() => _HomeScreenState();
}

class _HomeScreenState extends State<HomeScreen> {
  Uint8List? _liveImage;
  bool _liveLoading = false;

  @override
  void initState() {
    super.initState();
    _fetchLiveImage();
  }

  Future<void> _fetchLiveImage() async {
    if (_liveLoading) return;
    setState(() => _liveLoading = true);
    try {
      final res = await http
          .get(Uri.parse('${AppConfig.esp32CamBaseUrl}/latest.jpg'))
          .timeout(const Duration(seconds: 6));
      if (res.statusCode == 200 && mounted) {
        setState(() => _liveImage = res.bodyBytes);
      }
    } catch (_) {
      // Silently ignore — live view just stays blank
    } finally {
      if (mounted) setState(() => _liveLoading = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: AppColors.background,
      body: SafeArea(
        child: CustomScrollView(
          slivers: [
            // ── Top App Bar ────────────────────────────────────────────────
            SliverAppBar(
              floating: true,
              backgroundColor: AppColors.background,
              elevation: 0,
              scrolledUnderElevation: 0,
              titleSpacing: 16,
              title: Text(
                'Smart Fridge',
                style: Theme.of(context).textTheme.headlineMedium,
              ),
              actions: [
                IconButton(
                  onPressed: () {},
                  icon: const Icon(Icons.notifications_outlined,
                      color: AppColors.primary),
                ),
                const SizedBox(width: 4),
                Padding(
                  padding: const EdgeInsets.only(right: 16),
                  child: CircleAvatar(
                    radius: 18,
                    backgroundColor: AppColors.primaryFixed,
                    child: const Text(
                      'SF',
                      style: TextStyle(
                        fontSize: 12,
                        fontWeight: FontWeight.w700,
                        color: AppColors.primary,
                      ),
                    ),
                  ),
                ),
              ],
            ),

            SliverToBoxAdapter(
              child: Padding(
                padding: const EdgeInsets.symmetric(horizontal: 16),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    const SizedBox(height: 8),

                    // ── Greeting ────────────────────────────────────────────
                    Text(
                      'Hello, ${AuthService.displayName}!',
                      style: Theme.of(context).textTheme.titleLarge?.copyWith(
                            color: AppColors.onSurface,
                            fontWeight: FontWeight.w600,
                          ),
                    ),
                    const SizedBox(height: 2),
                    Text(
                      'Your fridge is running normally',
                      style: Theme.of(context).textTheme.bodyMedium,
                    ),
                    const SizedBox(height: 20),

                    // ── Status Cards Row (horizontal scroll) ────────────────
                    _StatusCardsRow(),
                    const SizedBox(height: 20),

                    // ── Live Fridge View ────────────────────────────────────
                    _LiveFridgeCard(
                      imageBytes: _liveImage,
                      loading: _liveLoading,
                      onTap: () => Navigator.push(context,
                          MaterialPageRoute(builder: (_) => const CameraScreen())),
                      onRefresh: _fetchLiveImage,
                    ),
                    const SizedBox(height: 20),

                    // ── Quick Actions ───────────────────────────────────────
                    Text(
                      'QUICK ACTIONS',
                      style: Theme.of(context).textTheme.labelLarge,
                    ),
                    const SizedBox(height: 12),
                    _QuickActionsGrid(onNavigate: widget.onNavigate),
                    const SizedBox(height: 20),

                    // ── Recent Activity ────────────────────────────────────
                    _RecentActivity(),
                    const SizedBox(height: 24),
                  ],
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }
}

// ── Status Cards ──────────────────────────────────────────────────────────────
class _StatusCardsRow extends StatelessWidget {
  @override
  Widget build(BuildContext context) {
    return SizedBox(
      height: 120,
      child: StreamBuilder<TemperatureReading?>(
        stream: FridgeService.temperatureStream(),
        builder: (ctx, tempSnap) {
          return StreamBuilder<InventorySnapshot?>(
            stream: FridgeService.inventoryStream(),
            builder: (ctx, invSnap) {
              final temp = tempSnap.data;
              final inv = invSnap.data;
              final total = inv?.items.length ?? 0;
              final low = inv?.items
                      .where((i) => i.confidence == 'low')
                      .length ??
                  0;

              return ListView(
                scrollDirection: Axis.horizontal,
                children: [
                  _StatusCard(
                    icon: Icons.thermostat_rounded,
                    label: 'Temperature',
                    value: temp?.temperatureC != null
                        ? '${temp!.temperatureC!.toStringAsFixed(1)}°C'
                        : '—',
                    status: temp == null
                        ? _CardStatus.neutral
                        : temp.isOk
                            ? _CardStatus.ok
                            : _CardStatus.alert,
                    onTap: () => Navigator.push(ctx,
                        MaterialPageRoute(
                            builder: (_) => const TemperatureScreen())),
                  ),
                  const SizedBox(width: 12),
                  _StatusCard(
                    icon: Icons.door_front_door_rounded,
                    label: 'Door',
                    value: 'Closed',
                    status: _CardStatus.ok,
                  ),
                  const SizedBox(width: 12),
                  _StatusCard(
                    icon: Icons.priority_high_rounded,
                    label: 'Expiring',
                    value: low > 0 ? '$low items' : 'None',
                    status: low > 0 ? _CardStatus.alert : _CardStatus.ok,
                    onTap: () => Navigator.push(ctx,
                        MaterialPageRoute(
                            builder: (_) => const ExpiryScreen())),
                  ),
                  const SizedBox(width: 12),
                  _StatusCard(
                    icon: Icons.inventory_2_rounded,
                    label: 'Total',
                    value: '$total items',
                    status: _CardStatus.neutral,
                  ),
                  const SizedBox(width: 4),
                ],
              );
            },
          );
        },
      ),
    );
  }
}

enum _CardStatus { ok, alert, neutral }

class _StatusCard extends StatelessWidget {
  final IconData icon;
  final String label;
  final String value;
  final _CardStatus status;
  final VoidCallback? onTap;

  const _StatusCard({
    required this.icon,
    required this.label,
    required this.value,
    required this.status,
    this.onTap,
  });

  Color get _borderColor {
    switch (status) {
      case _CardStatus.ok:
        return AppColors.secondary;
      case _CardStatus.alert:
        return AppColors.error;
      case _CardStatus.neutral:
        return AppColors.primary;
    }
  }

  Color get _iconColor {
    switch (status) {
      case _CardStatus.ok:
        return AppColors.secondary;
      case _CardStatus.alert:
        return AppColors.error;
      case _CardStatus.neutral:
        return AppColors.primary;
    }
  }

  @override
  Widget build(BuildContext context) {
    return GestureDetector(
      onTap: onTap,
      child: Container(
      width: 130,
      padding: const EdgeInsets.all(14),
      decoration: BoxDecoration(
        color: AppColors.surfaceContainerLow,
        borderRadius: BorderRadius.circular(16),
        border: Border(top: BorderSide(color: _borderColor, width: 4)),
        boxShadow: const [
          BoxShadow(color: Color(0x0A000000), blurRadius: 8, offset: Offset(0, 2)),
        ],
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.center,
        children: [
          Icon(icon, color: _iconColor, size: 28),
          const SizedBox(height: 6),
          Text(
            label,
            style: Theme.of(context)
                .textTheme
                .labelLarge
                ?.copyWith(color: AppColors.onSurfaceVariant),
            textAlign: TextAlign.center,
          ),
          const SizedBox(height: 4),
          Text(
            value,
            style: TextStyle(
              fontSize: 15,
              fontWeight: FontWeight.w700,
              color: _iconColor,
            ),
            textAlign: TextAlign.center,
            maxLines: 1,
            overflow: TextOverflow.ellipsis,
          ),
        ],
      ),
      ),
    );
  }
}

// ── Live Fridge Card ──────────────────────────────────────────────────────────
class _LiveFridgeCard extends StatelessWidget {
  final Uint8List? imageBytes;
  final bool loading;
  final VoidCallback onTap;
  final VoidCallback onRefresh;

  const _LiveFridgeCard({
    required this.imageBytes,
    required this.loading,
    required this.onTap,
    required this.onRefresh,
  });

  @override
  Widget build(BuildContext context) {
    return GestureDetector(
      onTap: onTap,
      child: ClipRRect(
        borderRadius: BorderRadius.circular(20),
        child: SizedBox(
          height: 180,
          width: double.infinity,
          child: Stack(
            fit: StackFit.expand,
            children: [
              // Background
              imageBytes != null
                  ? Image.memory(imageBytes!, fit: BoxFit.cover)
                  : Container(
                      color: AppColors.surfaceContainerHighest,
                      child: Icon(
                        Icons.videocam_rounded,
                        size: 64,
                        color: AppColors.outline,
                      ),
                    ),

              // Gradient overlay
              Container(
                decoration: const BoxDecoration(
                  gradient: LinearGradient(
                    begin: Alignment.topCenter,
                    end: Alignment.bottomCenter,
                    colors: [Colors.transparent, Color(0x99000000)],
                    stops: [0.4, 1.0],
                  ),
                ),
              ),

              // Labels
              Positioned(
                bottom: 14,
                left: 16,
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      'INSIDE VIEW',
                      style: const TextStyle(
                        color: Colors.white70,
                        fontSize: 11,
                        fontWeight: FontWeight.w600,
                        letterSpacing: 0.5,
                      ),
                    ),
                    const SizedBox(height: 2),
                    const Text(
                      'Live Camera Feed',
                      style: TextStyle(
                        color: Colors.white,
                        fontSize: 18,
                        fontWeight: FontWeight.w700,
                      ),
                    ),
                  ],
                ),
              ),

              // LIVE badge
              Positioned(
                top: 12,
                right: 12,
                child: Container(
                  padding:
                      const EdgeInsets.symmetric(horizontal: 10, vertical: 5),
                  decoration: BoxDecoration(
                    color: AppColors.error,
                    borderRadius: BorderRadius.circular(20),
                  ),
                  child: Row(
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      Container(
                        width: 6,
                        height: 6,
                        decoration: const BoxDecoration(
                          color: Colors.white,
                          shape: BoxShape.circle,
                        ),
                      ),
                      const SizedBox(width: 5),
                      const Text(
                        'LIVE',
                        style: TextStyle(
                          color: Colors.white,
                          fontSize: 10,
                          fontWeight: FontWeight.w800,
                          letterSpacing: 1,
                        ),
                      ),
                    ],
                  ),
                ),
              ),

              // Refresh button
              Positioned(
                top: 8,
                left: 8,
                child: Material(
                  color: Colors.black38,
                  borderRadius: BorderRadius.circular(20),
                  child: InkWell(
                    onTap: onRefresh,
                    borderRadius: BorderRadius.circular(20),
                    child: Padding(
                      padding: const EdgeInsets.all(6),
                      child: loading
                          ? const SizedBox(
                              width: 18,
                              height: 18,
                              child: CircularProgressIndicator(
                                strokeWidth: 2,
                                color: Colors.white,
                              ),
                            )
                          : const Icon(Icons.refresh_rounded,
                              color: Colors.white, size: 18),
                    ),
                  ),
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}

// ── Quick Actions ─────────────────────────────────────────────────────────────
class _QuickActionsGrid extends StatelessWidget {
  final void Function(int index) onNavigate;

  const _QuickActionsGrid({required this.onNavigate});

  @override
  Widget build(BuildContext context) {
    return Row(
      children: [
        _QuickAction(
          icon: Icons.warning_amber_rounded,
          label: 'Expiry\nAlerts',
          color: AppColors.errorContainer,
          iconColor: AppColors.error,
          onTap: () => Navigator.push(context,
              MaterialPageRoute(builder: (_) => const ExpiryScreen())),
        ),
        const SizedBox(width: 12),
        _QuickAction(
          icon: Icons.inventory_2_rounded,
          label: 'Inventory',
          color: AppColors.secondaryContainer,
          iconColor: AppColors.onSecondaryContainer,
          onTap: () => onNavigate(1),
        ),
        const SizedBox(width: 12),
        _QuickAction(
          icon: Icons.videocam_rounded,
          label: 'Live\nView',
          color: AppColors.primaryFixed,
          iconColor: AppColors.primary,
          onTap: () => Navigator.push(context,
              MaterialPageRoute(builder: (_) => const CameraScreen())),
        ),
      ],
    );
  }
}

class _QuickAction extends StatelessWidget {
  final IconData icon;
  final String label;
  final Color color;
  final Color iconColor;
  final VoidCallback onTap;

  const _QuickAction({
    required this.icon,
    required this.label,
    required this.color,
    required this.iconColor,
    required this.onTap,
  });

  @override
  Widget build(BuildContext context) {
    return Expanded(
      child: GestureDetector(
        onTap: onTap,
        child: Container(
          padding: const EdgeInsets.symmetric(vertical: 16, horizontal: 8),
          decoration: BoxDecoration(
            color: AppColors.surfaceContainerHighest,
            borderRadius: BorderRadius.circular(20),
          ),
          child: Column(
            children: [
              Container(
                width: 48,
                height: 48,
                decoration: BoxDecoration(
                  color: color,
                  shape: BoxShape.circle,
                ),
                child: Icon(icon, color: iconColor, size: 22),
              ),
              const SizedBox(height: 8),
              Text(
                label,
                style: Theme.of(context).textTheme.labelLarge?.copyWith(
                      color: AppColors.onSurface,
                    ),
                textAlign: TextAlign.center,
              ),
            ],
          ),
        ),
      ),
    );
  }
}

// ── Recent Activity ───────────────────────────────────────────────────────────
class _RecentActivity extends StatelessWidget {
  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.all(20),
      decoration: BoxDecoration(
        color: AppColors.surfaceContainerLowest,
        borderRadius: BorderRadius.circular(20),
        border: Border.all(color: AppColors.outlineVariant.withValues(alpha: 0.5)),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            mainAxisAlignment: MainAxisAlignment.spaceBetween,
            children: [
              Text(
                'Recent Activity',
                style: Theme.of(context).textTheme.titleLarge,
              ),
              Icon(Icons.history_rounded, color: AppColors.onSurfaceVariant),
            ],
          ),
          const SizedBox(height: 16),
          StreamBuilder<List<ScanRecord>>(
            stream: FridgeService.scanHistoryStream(),
            builder: (_, snap) {
              final scans = snap.data ?? [];
              if (scans.isEmpty) {
                return _ActivityItem(
                  icon: Icons.info_outline_rounded,
                  iconColor: AppColors.onSurfaceVariant,
                  bgColor: AppColors.surfaceContainerHigh,
                  title: 'No scans yet',
                  subtitle: 'Close the fridge door to trigger the first scan',
                );
              }
              final latest = scans.first;
              return Column(
                children: [
                  _ActivityItem(
                    icon: Icons.qr_code_scanner_rounded,
                    iconColor: AppColors.primary,
                    bgColor: AppColors.surfaceContainer,
                    title: 'Last scan: ${latest.timestamp}',
                    subtitle:
                        'System detected ${latest.itemCount} item${latest.itemCount == 1 ? '' : 's'}',
                  ),
                  if (scans.length > 1) ...[
                    const SizedBox(height: 10),
                    _ActivityItem(
                      icon: Icons.check_circle_rounded,
                      iconColor: AppColors.onSurfaceVariant,
                      bgColor: AppColors.surfaceContainer,
                      title: 'Previous scan: ${scans[1].timestamp}',
                      subtitle: '${scans[1].itemCount} items detected',
                      dimmed: true,
                    ),
                  ],
                ],
              );
            },
          ),
        ],
      ),
    );
  }
}

class _ActivityItem extends StatelessWidget {
  final IconData icon;
  final Color iconColor;
  final Color bgColor;
  final String title;
  final String subtitle;
  final bool dimmed;

  const _ActivityItem({
    required this.icon,
    required this.iconColor,
    required this.bgColor,
    required this.title,
    required this.subtitle,
    this.dimmed = false,
  });

  @override
  Widget build(BuildContext context) {
    return Opacity(
      opacity: dimmed ? 0.6 : 1.0,
      child: Container(
        padding: const EdgeInsets.all(12),
        decoration: BoxDecoration(
          color: bgColor,
          borderRadius: BorderRadius.circular(12),
        ),
        child: Row(
          children: [
            Container(
              padding: const EdgeInsets.all(8),
              decoration: BoxDecoration(
                color: AppColors.surfaceContainerLowest,
                shape: BoxShape.circle,
              ),
              child: Icon(icon, color: iconColor, size: 20),
            ),
            const SizedBox(width: 12),
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    title,
                    style: Theme.of(context).textTheme.bodyMedium?.copyWith(
                          fontWeight: FontWeight.w600,
                          color: AppColors.onSurface,
                        ),
                  ),
                  const SizedBox(height: 2),
                  Text(
                    subtitle,
                    style: Theme.of(context).textTheme.labelLarge?.copyWith(
                          color: AppColors.onSurfaceVariant,
                        ),
                  ),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }
}
