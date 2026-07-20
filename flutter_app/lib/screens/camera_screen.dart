import 'dart:async';
import 'dart:typed_data';
import 'package:flutter/material.dart';
import '../config.dart';
import '../models/fridge_item.dart';
import '../services/fridge_service.dart';
import '../theme/app_theme.dart';

// How long to wait for a fresh photo after requesting one before showing a
// timeout error — mirrors LIVEVIEW_TIMEOUT_MS on the CH board's ESP-NOW Live
// View (same idea, applied to the RTDB+Firestore round trip this screen uses).
const _liveViewTimeout = Duration(seconds: 10);

class CameraScreen extends StatefulWidget {
  const CameraScreen({super.key});

  @override
  State<CameraScreen> createState() => _CameraScreenState();
}

class _CameraScreenState extends State<CameraScreen> {
  int _roof = 1;
  bool _requesting = false;
  String _error = '';
  Timer? _timeoutTimer;

  // capturedAt already stored in Firestore when the request went out —
  // an emission with the same value is the stale photo, not our reply.
  String? _staleCapturedAt;

  // Held in state so rebuilds don't re-subscribe; recreated on roof switch.
  late Stream<({Uint8List? photo, String? capturedAt})> _liveStream =
      FridgeService.liveViewPhotoStream(_roof);
  late final Stream<InventorySnapshot?> _invStream =
      FridgeService.inventoryStream();
  late final Stream<TemperatureReading?> _tempStream =
      FridgeService.temperatureStream();

  @override
  void initState() {
    super.initState();
    _requestSnapshot();
  }

  @override
  void dispose() {
    _timeoutTimer?.cancel();
    super.dispose();
  }

  Future<void> _requestSnapshot() async {
    _timeoutTimer?.cancel();
    _staleCapturedAt = await FridgeService.currentLiveViewCapturedAt(_roof);
    if (!mounted) return;
    setState(() {
      _requesting = true;
      _error = '';
    });
    try {
      await FridgeService.requestLiveViewSnapshot(_roof);
    } catch (_) {
      if (mounted) {
        setState(() {
          _requesting = false;
          _error = 'Could not reach Firebase — check your connection.';
        });
      }
      return;
    }
    _timeoutTimer = Timer(_liveViewTimeout, () {
      if (mounted && _requesting) {
        setState(() {
          _requesting = false;
          _error = 'No response from roof $_roof — is that camera online?';
        });
      }
    });
  }

  void _onPhotoArrived() {
    if (!_requesting) return;
    _timeoutTimer?.cancel();
    setState(() => _requesting = false);
  }

  void _switchRoof() {
    setState(() {
      _roof = _roof % AppConfig.numRoofs + 1;
      _liveStream = FridgeService.liveViewPhotoStream(_roof);
    });
    _requestSnapshot();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: AppColors.background,
      body: SafeArea(
        child: Padding(
          padding: const EdgeInsets.symmetric(horizontal: 16),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              // ── Header ─────────────────────────────────────────────────
              const SizedBox(height: 8),
              Row(
                children: [
                  IconButton(
                    onPressed: () => Navigator.pop(context),
                    icon: const Icon(Icons.arrow_back_rounded,
                        color: AppColors.onSurface),
                  ),
                  Text('Live View',
                      style: Theme.of(context).textTheme.headlineMedium),
                  const Spacer(),
                  if (AppConfig.numRoofs > 1)
                    IconButton(
                      onPressed: _requesting ? null : _switchRoof,
                      icon: const Icon(Icons.switch_camera_rounded,
                          color: AppColors.primary),
                      tooltip: 'Switch Roof',
                    ),
                  IconButton(
                    onPressed: _requesting ? null : _requestSnapshot,
                    icon: _requesting
                        ? const SizedBox(
                            width: 20,
                            height: 20,
                            child: CircularProgressIndicator(
                                strokeWidth: 2,
                                color: AppColors.primary),
                          )
                        : const Icon(Icons.refresh_rounded,
                            color: AppColors.primary),
                    tooltip: 'Refresh',
                  ),
                ],
              ),
              Text(
                AppConfig.numRoofs > 1
                    ? 'Roof $_roof of ${AppConfig.numRoofs}'
                    : 'Fridge camera',
                style: const TextStyle(
                    fontSize: 12, color: AppColors.onSurfaceVariant),
              ),
              const SizedBox(height: 16),

              // ── Image frame ─────────────────────────────────────────────
              Expanded(
                child: StreamBuilder<({Uint8List? photo, String? capturedAt})>(
                  stream: _liveStream,
                  builder: (context, snap) {
                    final photo = snap.data?.photo;
                    final capturedAt = snap.data?.capturedAt;
                    // Only a photo newer than the one stored at request time
                    // counts as the camera's reply — the stream's first
                    // emission is whatever was already in Firestore.
                    if (_requesting &&
                        photo != null &&
                        capturedAt != _staleCapturedAt) {
                      WidgetsBinding.instance
                          .addPostFrameCallback((_) => _onPhotoArrived());
                    }
                    final isLive = FridgeService.isRecentCapture(capturedAt);

                    return ClipRRect(
                      borderRadius: BorderRadius.circular(20),
                      child: Stack(
                        fit: StackFit.expand,
                        children: [
                          Container(
                            color: AppColors.surfaceContainerHighest,
                            child: photo != null
                                ? InteractiveViewer(
                                    child: Image.memory(photo,
                                        fit: BoxFit.contain))
                                : Center(
                                    child: Padding(
                                      padding: const EdgeInsets.all(32),
                                      child: Column(
                                        mainAxisSize: MainAxisSize.min,
                                        children: [
                                          Icon(
                                            _requesting
                                                ? Icons.hourglass_top_rounded
                                                : Icons.videocam_rounded,
                                            size: 72,
                                            color: AppColors.outline,
                                          ),
                                          const SizedBox(height: 16),
                                          Text(
                                            _error.isNotEmpty
                                                ? _error
                                                : _requesting
                                                    ? 'Requesting photo from roof $_roof...'
                                                    : 'Tap refresh to load the latest fridge photo',
                                            textAlign: TextAlign.center,
                                            style: TextStyle(
                                              color: _error.isNotEmpty
                                                  ? AppColors.error
                                                  : AppColors.onSurfaceVariant,
                                              fontSize: 14,
                                              height: 1.5,
                                            ),
                                          ),
                                        ],
                                      ),
                                    ),
                                  ),
                          ),

                          if (photo != null)
                            Positioned(
                              top: 12,
                              right: 12,
                              child: Container(
                                padding: const EdgeInsets.symmetric(
                                    horizontal: 10, vertical: 5),
                                decoration: BoxDecoration(
                                  color: isLive
                                      ? AppColors.error
                                      : Colors.black54,
                                  borderRadius: BorderRadius.circular(20),
                                ),
                                child: Row(
                                  mainAxisSize: MainAxisSize.min,
                                  children: [
                                    Icon(
                                        isLive
                                            ? Icons.circle
                                            : Icons.history_rounded,
                                        color: Colors.white,
                                        size: isLive ? 6 : 10),
                                    const SizedBox(width: 5),
                                    Text(
                                      isLive ? 'LIVE' : 'SNAPSHOT',
                                      style: const TextStyle(
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
                        ],
                      ),
                    );
                  },
                ),
              ),

              const SizedBox(height: 12),

              // Timestamp
              StreamBuilder<({Uint8List? photo, String? capturedAt})>(
                stream: _liveStream,
                builder: (context, snap) {
                  final capturedAt = snap.data?.capturedAt;
                  if (capturedAt == null) return const SizedBox.shrink();
                  return Center(
                    child: Text(
                      'Captured at $capturedAt',
                      style: const TextStyle(
                          fontSize: 11, color: AppColors.onSurfaceVariant),
                    ),
                  );
                },
              ),

              const SizedBox(height: 14),

              // Refresh button
              SizedBox(
                width: double.infinity,
                child: ElevatedButton.icon(
                  onPressed: _requesting ? null : _requestSnapshot,
                  icon: _requesting
                      ? const SizedBox(
                          width: 18,
                          height: 18,
                          child: CircularProgressIndicator(
                              strokeWidth: 2, color: Colors.white))
                      : const Icon(Icons.refresh_rounded),
                  label: const Text('Refresh Photo'),
                  style: ElevatedButton.styleFrom(
                    padding: const EdgeInsets.symmetric(vertical: 14),
                    shape: RoundedRectangleBorder(
                        borderRadius: BorderRadius.circular(14)),
                  ),
                ),
              ),

              const SizedBox(height: 16),

              // ── Insight cards ────────────────────────────────────────────
              StreamBuilder<InventorySnapshot?>(
                stream: _invStream,
                builder: (_, invSnap) {
                  return StreamBuilder<TemperatureReading?>(
                    stream: _tempStream,
                    builder: (_, tempSnap) {
                      final inv = invSnap.data;
                      final temp = tempSnap.data;
                      if (inv == null) return const SizedBox.shrink();

                      final fresh = inv.items
                          .where((i) => i.confidence == 'high')
                          .length;
                      final total = inv.items.length;
                      final freshPct =
                          total > 0 ? (fresh * 100 ~/ total) : 0;
                      final low = inv.items
                          .where((i) => i.confidence == 'low')
                          .toList();

                      return Column(
                        children: [
                          Row(
                            children: [
                              Expanded(
                                child: _InsightCard(
                                  icon: Icons.eco_rounded,
                                  color: AppColors.secondary,
                                  title: 'Scan Quality',
                                  value: '$freshPct%',
                                  subtitle: 'items clearly detected',
                                ),
                              ),
                              const SizedBox(width: 12),
                              Expanded(
                                child: _InsightCard(
                                  icon: Icons.thermostat_rounded,
                                  color: temp?.isAlert == true
                                      ? AppColors.error
                                      : const Color(0xFF0288D1),
                                  title: 'Temperature',
                                  value: temp?.temperatureC != null
                                      ? '${temp!.temperatureC!.toStringAsFixed(1)}°C'
                                      : '—',
                                  subtitle: temp?.temperatureC == null
                                      ? 'No sensor data'
                                      : temp!.isAlert
                                          ? 'Out of range'
                                          : 'Optimal',
                                ),
                              ),
                            ],
                          ),
                          if (low.isNotEmpty) ...[
                            const SizedBox(height: 12),
                            Container(
                              padding: const EdgeInsets.all(12),
                              decoration: BoxDecoration(
                                color: AppColors.errorContainer,
                                borderRadius: BorderRadius.circular(14),
                              ),
                              child: Row(
                                children: [
                                  const Icon(Icons.warning_rounded,
                                      color: AppColors.error, size: 18),
                                  const SizedBox(width: 8),
                                  Expanded(
                                    child: Text(
                                      'Uncertain detection: ${low.map((i) => i.displayName).join(', ')}',
                                      style: const TextStyle(
                                          fontSize: 12,
                                          color: AppColors.error,
                                          fontWeight: FontWeight.w600),
                                    ),
                                  ),
                                ],
                              ),
                            ),
                          ],
                        ],
                      );
                    },
                  );
                },
              ),

              const SizedBox(height: 16),
            ],
          ),
        ),
      ),
    );
  }
}

class _InsightCard extends StatelessWidget {
  final IconData icon;
  final Color color;
  final String title;
  final String value;
  final String subtitle;

  const _InsightCard({
    required this.icon,
    required this.color,
    required this.title,
    required this.value,
    required this.subtitle,
  });

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.all(14),
      decoration: BoxDecoration(
        color: color.withValues(alpha: 0.1),
        borderRadius: BorderRadius.circular(14),
        border: Border.all(color: color.withValues(alpha: 0.2)),
      ),
      child: Row(
        children: [
          Icon(icon, size: 24, color: color),
          const SizedBox(width: 10),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(title,
                    style: const TextStyle(
                        fontSize: 11,
                        color: AppColors.onSurfaceVariant)),
                Text(value,
                    style: TextStyle(
                        fontSize: 18,
                        fontWeight: FontWeight.w800,
                        color: color)),
                Text(subtitle,
                    style: const TextStyle(
                        fontSize: 10,
                        color: AppColors.onSurfaceVariant)),
              ],
            ),
          ),
        ],
      ),
    );
  }
}
