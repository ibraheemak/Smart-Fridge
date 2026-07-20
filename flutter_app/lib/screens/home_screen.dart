import 'dart:async';
import 'dart:typed_data';
import 'package:flutter/material.dart';
import '../models/fridge_item.dart';
import '../models/scan_record.dart';
import '../services/auth_service.dart';
import '../services/fridge_service.dart';
import '../theme/app_theme.dart';
import '../widgets/app_top_bar.dart';
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
  bool _liveRequesting = false;
  Timer? _liveTimeout;

  // capturedAt of the photo already stored in Firestore when the request
  // went out — a stream emission with the SAME value is the stale photo,
  // not the reply to our request.
  String? _staleCapturedAt;

  // Streams held here so parent rebuilds don't re-subscribe (which would
  // flash every card back to its loading state).
  late final Stream<TemperatureReading?> _tempStream =
      FridgeService.temperatureStream();
  late final Stream<DoorStatus?> _doorStream = FridgeService.doorStream();
  late final Stream<InventorySnapshot?> _invStream =
      FridgeService.inventoryStream();
  late final Stream<List<ScanRecord>> _scanStream =
      FridgeService.scanHistoryStream();
  late final Stream<({Uint8List? photo, String? capturedAt})> _liveStream =
      FridgeService.liveViewPhotoStream(1);

  @override
  void initState() {
    super.initState();
    _requestLiveView();
  }

  @override
  void dispose() {
    _liveTimeout?.cancel();
    super.dispose();
  }

  Future<void> _requestLiveView() async {
    if (_liveRequesting) return;
    _liveTimeout?.cancel();
    _staleCapturedAt = await FridgeService.currentLiveViewCapturedAt(1);
    if (!mounted) return;
    setState(() => _liveRequesting = true);
    try {
      await FridgeService.requestLiveViewSnapshot(1);
      _liveTimeout = Timer(const Duration(seconds: 12), () {
        if (mounted && _liveRequesting) setState(() => _liveRequesting = false);
      });
    } catch (_) {
      if (mounted) setState(() => _liveRequesting = false);
    }
  }

  void _onLivePhotoArrived() {
    _liveTimeout?.cancel();
    if (mounted && _liveRequesting) setState(() => _liveRequesting = false);
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: AppColors.background,
      body: SafeArea(
        child: CustomScrollView(
          slivers: [
            // ── Top App Bar ────────────────────────────────────────────────
            const AppTopBar(),

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
                    _StatusSubtitle(
                      tempStream: _tempStream,
                      doorStream: _doorStream,
                      invStream: _invStream,
                    ),
                    const SizedBox(height: 20),

                    // ── Status Cards Row ────────────────────────────────────
                    _StatusCardsRow(
                      tempStream: _tempStream,
                      doorStream: _doorStream,
                      invStream: _invStream,
                    ),
                    const SizedBox(height: 20),

                    // ── Live Fridge Card ────────────────────────────────────
                    _LiveFridgeCard(
                      stream: _liveStream,
                      requesting: _liveRequesting,
                      staleCapturedAt: _staleCapturedAt,
                      onPhotoArrived: _onLivePhotoArrived,
                      onRefresh: _requestLiveView,
                      onTap: () => Navigator.push(context,
                          MaterialPageRoute(builder: (_) => const CameraScreen())),
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

                    // ── Recent Activity ─────────────────────────────────────
                    _RecentActivity(scanStream: _scanStream),
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

// ── Status subtitle — reflects the real fridge state ──────────────────────────
class _StatusSubtitle extends StatelessWidget {
  final Stream<TemperatureReading?> tempStream;
  final Stream<DoorStatus?> doorStream;
  final Stream<InventorySnapshot?> invStream;

  const _StatusSubtitle({
    required this.tempStream,
    required this.doorStream,
    required this.invStream,
  });

  @override
  Widget build(BuildContext context) {
    return StreamBuilder<TemperatureReading?>(
      stream: tempStream,
      builder: (_, tempSnap) => StreamBuilder<DoorStatus?>(
        stream: doorStream,
        builder: (_, doorSnap) => StreamBuilder<InventorySnapshot?>(
          stream: invStream,
          builder: (_, invSnap) {
            final temp = tempSnap.data;
            final door = doorSnap.data;
            final expiring = invSnap.data?.items.where((i) {
                  final s = i.expiryStatus;
                  return s == ExpiryStatus.expired ||
                      s == ExpiryStatus.critical ||
                      s == ExpiryStatus.soon;
                }).length ??
                0;

            final String text;
            final bool alert;
            if (temp?.isAlert == true) {
              text = 'Temperature alert — check the fridge!';
              alert = true;
            } else if (door?.isOpen == true) {
              text = 'The fridge door is open';
              alert = true;
            } else if (expiring > 0) {
              text =
                  '$expiring item${expiring == 1 ? '' : 's'} need${expiring == 1 ? 's' : ''} attention';
              alert = true;
            } else if (temp == null && door == null) {
              text = 'Waiting for sensor data…';
              alert = false;
            } else {
              text = 'Everything looks good';
              alert = false;
            }

            return Text(
              text,
              style: Theme.of(context).textTheme.bodyMedium?.copyWith(
                    color: alert ? AppColors.error : null,
                    fontWeight: alert ? FontWeight.w600 : null,
                  ),
            );
          },
        ),
      ),
    );
  }
}

// ── Status Cards ──────────────────────────────────────────────────────────────
class _StatusCardsRow extends StatelessWidget {
  final Stream<TemperatureReading?> tempStream;
  final Stream<DoorStatus?> doorStream;
  final Stream<InventorySnapshot?> invStream;

  const _StatusCardsRow({
    required this.tempStream,
    required this.doorStream,
    required this.invStream,
  });

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      height: 120,
      child: StreamBuilder<TemperatureReading?>(
        stream: tempStream,
        builder: (ctx, tempSnap) {
          return StreamBuilder<DoorStatus?>(
            stream: doorStream,
            builder: (ctx, doorSnap) {
              return StreamBuilder<InventorySnapshot?>(
                stream: invStream,
                builder: (ctx, invSnap) {
                  final temp = tempSnap.data;
                  final door = doorSnap.data;
                  final inv = invSnap.data;
                  final total = inv?.items.length ?? 0;
                  final expiringCount = inv?.items.where((i) {
                    final s = i.expiryStatus;
                    return s == ExpiryStatus.expired ||
                        s == ExpiryStatus.critical ||
                        s == ExpiryStatus.soon;
                  }).length ?? 0;

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
                        value: door == null
                            ? '—'
                            : door.isOpen
                                ? 'Open'
                                : 'Closed',
                        status: door == null
                            ? _CardStatus.neutral
                            : door.isOpen
                                ? _CardStatus.alert
                                : _CardStatus.ok,
                        onTap: () => Navigator.push(ctx,
                            MaterialPageRoute(
                                builder: (_) => const TemperatureScreen())),
                      ),
                      const SizedBox(width: 12),
                      _StatusCard(
                        icon: Icons.priority_high_rounded,
                        label: 'Expiring',
                        value: expiringCount > 0 ? '$expiringCount items' : 'None',
                        status: expiringCount > 0 ? _CardStatus.alert : _CardStatus.ok,
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
  final Stream<({Uint8List? photo, String? capturedAt})> stream;
  final bool requesting;
  final String? staleCapturedAt;
  final VoidCallback onPhotoArrived;
  final VoidCallback onTap;
  final VoidCallback onRefresh;

  const _LiveFridgeCard({
    required this.stream,
    required this.requesting,
    required this.staleCapturedAt,
    required this.onPhotoArrived,
    required this.onTap,
    required this.onRefresh,
  });

  @override
  Widget build(BuildContext context) {
    return StreamBuilder<({Uint8List? photo, String? capturedAt})>(
      stream: stream,
      builder: (_, snap) {
        final photo = snap.data?.photo;
        final capturedAt = snap.data?.capturedAt;
        // Only a photo NEWER than what was stored when we asked counts as
        // the reply — the stream's first emission is the old stored photo.
        if (requesting && photo != null && capturedAt != staleCapturedAt) {
          WidgetsBinding.instance.addPostFrameCallback((_) => onPhotoArrived());
        }
        final isLive = FridgeService.isRecentCapture(capturedAt);

        return GestureDetector(
          onTap: onTap,
          child: ClipRRect(
            borderRadius: BorderRadius.circular(20),
            child: SizedBox(
              height: 260,
              width: double.infinity,
              child: Stack(
                fit: StackFit.expand,
                children: [
                  // Background — show the whole rotated frame (letterboxed
                  // over black) so nothing gets cropped.
                  photo != null
                      ? Container(
                          color: Colors.black,
                          alignment: Alignment.center,
                          // Camera is mounted rotated — turn the feed 90° CW.
                          child: RotatedBox(
                            quarterTurns: 1,
                            child: Image.memory(photo, fit: BoxFit.contain),
                          ),
                        )
                      : Container(
                          color: AppColors.surfaceContainerHighest,
                          child: const Icon(
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
                  const Positioned(
                    bottom: 14,
                    left: 16,
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Text(
                          'INSIDE VIEW',
                          style: TextStyle(
                            color: Colors.white70,
                            fontSize: 11,
                            fontWeight: FontWeight.w600,
                            letterSpacing: 0.5,
                          ),
                        ),
                        SizedBox(height: 2),
                        Text(
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

                  // LIVE badge — only for a photo captured moments ago
                  if (photo != null && isLive)
                    Positioned(
                      top: 12,
                      right: 12,
                      child: Container(
                        padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 5),
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
                          child: requesting
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
      },
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
  final Stream<List<ScanRecord>> scanStream;

  const _RecentActivity({required this.scanStream});

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
              const Icon(Icons.history_rounded, color: AppColors.onSurfaceVariant),
            ],
          ),
          const SizedBox(height: 16),
          StreamBuilder<List<ScanRecord>>(
            stream: scanStream,
            builder: (_, snap) {
              final scans = snap.data ?? [];
              if (scans.isEmpty) {
                return const _ActivityItem(
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
              decoration: const BoxDecoration(
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
