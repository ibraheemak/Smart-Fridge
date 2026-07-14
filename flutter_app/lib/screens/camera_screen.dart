import 'dart:async';
import 'dart:typed_data';
import 'package:flutter/material.dart';
import 'package:http/http.dart' as http;
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
  bool _scanning = false;
  String _error = '';
  Timer? _timeoutTimer;

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
    setState(() => _roof = _roof % AppConfig.numRoofs + 1);
    _requestSnapshot();
  }

  Future<void> _runAiScan() async {
    if (_scanning) return;
    setState(() => _scanning = true);
    try {
      await http
          .get(Uri.parse('${AppConfig.esp32CamBaseUrl}/scan'))
          .timeout(const Duration(seconds: 30));
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(
            content: Text('AI scan triggered! Updating inventory...'),
            backgroundColor: AppColors.secondary,
          ),
        );
      }
    } catch (_) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(
            content: Text('Cannot reach ESP32-CAM to trigger scan.'),
            backgroundColor: AppColors.error,
          ),
        );
      }
    } finally {
      if (mounted) setState(() => _scanning = false);
    }
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
                  stream: FridgeService.liveViewPhotoStream(_roof),
                  builder: (context, snap) {
                    final photo = snap.data?.photo;
                    if (snap.hasData) {
                      // Defer to after this build so we don't call setState
                      // while the widget tree is still building.
                      WidgetsBinding.instance
                          .addPostFrameCallback((_) => _onPhotoArrived());
                    }

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

                          // LIVE badge (when a photo is loaded)
                          if (photo != null)
                            Positioned(
                              top: 12,
                              right: 12,
                              child: Container(
                                padding: const EdgeInsets.symmetric(
                                    horizontal: 10, vertical: 5),
                                decoration: BoxDecoration(
                                  color: AppColors.error,
                                  borderRadius: BorderRadius.circular(20),
                                ),
                                child: Row(
                                  mainAxisSize: MainAxisSize.min,
                                  children: const [
                                    Icon(Icons.circle,
                                        color: Colors.white, size: 6),
                                    SizedBox(width: 5),
                                    Text(
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
                        ],
                      ),
                    );
                  },
                ),
              ),

              const SizedBox(height: 12),

              // Timestamp
              StreamBuilder<({Uint8List? photo, String? capturedAt})>(
                stream: FridgeService.liveViewPhotoStream(_roof),
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

              // Buttons row
              Row(
                children: [
                  Expanded(
                    child: ElevatedButton.icon(
                      onPressed: _requesting ? null : _requestSnapshot,
                      icon: _requesting
                          ? const SizedBox(
                              width: 18,
                              height: 18,
                              child: CircularProgressIndicator(
                                  strokeWidth: 2, color: Colors.white))
                          : const Icon(Icons.refresh_rounded),
                      label: const Text('Refresh'),
                      style: ElevatedButton.styleFrom(
                        padding: const EdgeInsets.symmetric(vertical: 14),
                        shape: RoundedRectangleBorder(
                            borderRadius: BorderRadius.circular(14)),
                      ),
                    ),
                  ),
                  const SizedBox(width: 10),
                  Expanded(
                    child: FilledButton.icon(
                      onPressed: _scanning ? null : _runAiScan,
                      icon: _scanning
                          ? const SizedBox(
                              width: 18,
                              height: 18,
                              child: CircularProgressIndicator(
                                  strokeWidth: 2, color: Colors.white))
                          : const Icon(Icons.auto_awesome_rounded),
                      label: Text(_scanning ? 'Scanning...' : 'Run AI Scan'),
                      style: FilledButton.styleFrom(
                        backgroundColor: AppColors.secondary,
                        padding: const EdgeInsets.symmetric(vertical: 14),
                        shape: RoundedRectangleBorder(
                            borderRadius: BorderRadius.circular(14)),
                      ),
                    ),
                  ),
                ],
              ),

              const SizedBox(height: 16),

              // ── Insight cards ────────────────────────────────────────────
              StreamBuilder<InventorySnapshot?>(
                stream: FridgeService.inventoryStream(),
                builder: (_, invSnap) {
                  return StreamBuilder<TemperatureReading?>(
                    stream: FridgeService.temperatureStream(),
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
                                  title: 'Freshness',
                                  value: '$freshPct%',
                                  subtitle: 'items are fresh',
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
                                  subtitle: temp?.isOk == true
                                      ? 'Optimal'
                                      : 'Check sensor',
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
                                      'Low: ${low.map((i) => i.displayName).join(', ')}',
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
