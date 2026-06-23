import 'dart:async';
import 'dart:typed_data';
import 'package:cached_network_image/cached_network_image.dart';
import 'package:flutter/material.dart';
import 'package:http/http.dart' as http;
import '../config.dart';
import '../models/fridge_item.dart';
import '../services/fridge_service.dart';
import '../theme/app_theme.dart';

class CameraScreen extends StatefulWidget {
  const CameraScreen({super.key});

  @override
  State<CameraScreen> createState() => _CameraScreenState();
}

class _CameraScreenState extends State<CameraScreen> {
  // Single Firestore subscription — avoids duplicate listeners
  StreamSubscription<FridgeSnapshot?>? _snapshotSub;
  FridgeSnapshot? _snapshot;

  // Direct-from-ESP32 bytes (same-WiFi only, manual refresh)
  Uint8List? _localBytes;
  bool _loadingLocal = false;
  bool _showLocal = false;    // true after user explicitly taps "From ESP32"
  String _localError = '';

  String _camUrl = AppConfig.esp32CamBaseUrl;
  bool _scanning = false;

  @override
  void initState() {
    super.initState();
    _snapshotSub = FridgeService.snapshotStream().listen((s) {
      if (mounted) setState(() => _snapshot = s);
    });
  }

  @override
  void dispose() {
    _snapshotSub?.cancel();
    super.dispose();
  }

  // Fetch JPEG directly from ESP32 (works only on same WiFi as fridge)
  Future<void> _refreshFromESP32() async {
    if (_loadingLocal) return;
    setState(() {
      _loadingLocal = true;
      _localError = '';
      _showLocal = true;
    });
    try {
      final res = await http
          .get(Uri.parse('$_camUrl/latest.jpg'))
          .timeout(const Duration(seconds: 10));
      if (res.statusCode == 200) {
        setState(() => _localBytes = res.bodyBytes);
      } else {
        setState(() => _localError =
            'No image yet — close the door to trigger a scan.');
      }
    } catch (_) {
      setState(() => _localError =
          'Cannot reach ESP32-CAM.\n'
          '• Must be on the same WiFi as the fridge.\n'
          '• Tap ⚙ to update the IP address.\n'
          '• Current: $_camUrl');
    } finally {
      if (mounted) setState(() => _loadingLocal = false);
    }
  }

  Future<void> _runAiScan() async {
    if (_scanning) return;
    setState(() => _scanning = true);
    try {
      await http
          .get(Uri.parse('$_camUrl/scan'))
          .timeout(const Duration(seconds: 30));
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(
            content: Text('AI scan triggered! Inventory will update shortly…'),
            backgroundColor: AppColors.secondary,
          ),
        );
      }
    } catch (_) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(
            content:
                Text('Cannot reach ESP32-CAM. Scan on door close still works.'),
            backgroundColor: AppColors.error,
          ),
        );
      }
    } finally {
      if (mounted) setState(() => _scanning = false);
    }
  }

  void _showIpDialog() {
    final ctrl = TextEditingController(text: _camUrl);
    showDialog(
      context: context,
      builder: (_) => AlertDialog(
        title: const Text('ESP32-CAM IP Address'),
        content: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(
              'Only needed for "From ESP32" — the cloud photo works anywhere.\n\n'
              'Find the IP in Arduino Serial Monitor on boot:\n'
              '[WEB] http://192.168.x.x/scan',
              style:
                  TextStyle(fontSize: 12, color: AppColors.onSurfaceVariant),
            ),
            const SizedBox(height: 12),
            TextField(
              controller: ctrl,
              keyboardType: TextInputType.url,
              decoration: const InputDecoration(
                hintText: 'http://192.168.1.100',
                prefixIcon:
                    Icon(Icons.wifi_rounded, color: AppColors.primary),
              ),
            ),
          ],
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(context),
            child: Text('Cancel',
                style: TextStyle(color: AppColors.onSurfaceVariant)),
          ),
          FilledButton(
            onPressed: () {
              setState(() => _camUrl = ctrl.text.trim());
              Navigator.pop(context);
            },
            child: const Text('Save'),
          ),
        ],
      ),
    );
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
              // ── Header ──────────────────────────────────────────────────
              const SizedBox(height: 8),
              Row(
                children: [
                  Text('Live View',
                      style: Theme.of(context).textTheme.headlineMedium),
                  const Spacer(),
                  IconButton(
                    onPressed: _showIpDialog,
                    icon: const Icon(Icons.settings_rounded,
                        color: AppColors.primary),
                    tooltip: 'Set ESP32-CAM IP',
                  ),
                ],
              ),

              // ── Photo frame ─────────────────────────────────────────────
              Expanded(
                child: ClipRRect(
                  borderRadius: BorderRadius.circular(20),
                  child: Stack(
                    fit: StackFit.expand,
                    children: [
                      Container(color: AppColors.surfaceContainerHighest),

                      // Priority: direct ESP32 bytes → Firestore cloud photo → placeholder
                      if (_showLocal && _localBytes != null)
                        InteractiveViewer(
                          child:
                              Image.memory(_localBytes!, fit: BoxFit.contain),
                        )
                      else if (_snapshot != null &&
                          _snapshot!.url.isNotEmpty)
                        _FirestorePhoto(url: _snapshot!.url)
                      else
                        _Placeholder(
                          loading: _snapshotSub != null && _snapshot == null,
                          error: _localError,
                        ),

                      // Source badge
                      Positioned(
                        top: 12,
                        right: 12,
                        child: _showLocal && _localBytes != null
                            ? const _Badge(
                                label: 'DIRECT',
                                color: AppColors.secondary)
                            : _snapshot != null
                                ? const _Badge(
                                    label: 'CLOUD',
                                    color: AppColors.primary)
                                : const SizedBox.shrink(),
                      ),

                      // Local error overlay (shown even when cloud photo is behind)
                      if (_showLocal && _localError.isNotEmpty)
                        Positioned(
                          bottom: 0,
                          left: 0,
                          right: 0,
                          child: Container(
                            color: AppColors.error.withValues(alpha: 0.88),
                            padding: const EdgeInsets.all(12),
                            child: Text(
                              _localError,
                              style: const TextStyle(
                                  color: Colors.white, fontSize: 12),
                            ),
                          ),
                        ),
                    ],
                  ),
                ),
              ),

              const SizedBox(height: 10),

              // ── Timestamp / item count ─────────────────────────────────
              if (_snapshot != null)
                Center(
                  child: Text(
                    _snapshot!.itemCount > 0
                        ? 'Last scan: ${_snapshot!.itemCount} items  •  ${_snapshot!.timestamp}'
                        : 'Last scan: ${_snapshot!.timestamp}',
                    style: const TextStyle(
                        fontSize: 11, color: AppColors.onSurfaceVariant),
                  ),
                ),

              const SizedBox(height: 12),

              // ── Action buttons ───────────────────────────────────────────
              Row(
                children: [
                  Expanded(
                    child: OutlinedButton.icon(
                      onPressed: _loadingLocal ? null : _refreshFromESP32,
                      icon: _loadingLocal
                          ? const SizedBox(
                              width: 16,
                              height: 16,
                              child: CircularProgressIndicator(
                                  strokeWidth: 2, color: AppColors.primary))
                          : const Icon(Icons.wifi_rounded, size: 18),
                      label: const Text('From ESP32'),
                      style: OutlinedButton.styleFrom(
                        padding: const EdgeInsets.symmetric(vertical: 14),
                        side: const BorderSide(color: AppColors.primary),
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
                              width: 16,
                              height: 16,
                              child: CircularProgressIndicator(
                                  strokeWidth: 2, color: Colors.white))
                          : const Icon(Icons.auto_awesome_rounded, size: 18),
                      label: Text(_scanning ? 'Scanning…' : 'Run AI Scan'),
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

              const SizedBox(height: 14),

              // ── Insight cards (inventory + temp summary) ─────────────────
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
                                      'Low confidence: ${low.map((i) => i.displayName).join(', ')}',
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

// ── Firebase Storage photo (cached, works from any network) ──────────────────
class _FirestorePhoto extends StatelessWidget {
  final String url;
  const _FirestorePhoto({required this.url});

  @override
  Widget build(BuildContext context) {
    return CachedNetworkImage(
      imageUrl: url,
      fit: BoxFit.contain,
      fadeInDuration: const Duration(milliseconds: 300),
      placeholder: (_, __) => const Center(
        child: CircularProgressIndicator(color: AppColors.primary),
      ),
      errorWidget: (_, __, ___) => const _Placeholder(
        loading: false,
        error: 'Could not load photo from cloud.',
      ),
    );
  }
}

// ── Status badge ─────────────────────────────────────────────────────────────
class _Badge extends StatelessWidget {
  final String label;
  final Color color;
  const _Badge({required this.label, required this.color});

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 5),
      decoration: BoxDecoration(
        color: color,
        borderRadius: BorderRadius.circular(20),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          Icon(
            label == 'CLOUD' ? Icons.cloud_done_rounded : Icons.wifi_rounded,
            color: Colors.white,
            size: 10,
          ),
          const SizedBox(width: 4),
          Text(
            label,
            style: const TextStyle(
              color: Colors.white,
              fontSize: 10,
              fontWeight: FontWeight.w800,
              letterSpacing: 1,
            ),
          ),
        ],
      ),
    );
  }
}

// ── Placeholder / error ───────────────────────────────────────────────────────
class _Placeholder extends StatelessWidget {
  final bool loading;
  final String error;
  const _Placeholder({required this.loading, this.error = ''});

  @override
  Widget build(BuildContext context) {
    return Center(
      child: Padding(
        padding: const EdgeInsets.all(32),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            if (loading)
              const CircularProgressIndicator(color: AppColors.primary)
            else
              Icon(
                error.isEmpty
                    ? Icons.videocam_rounded
                    : Icons.cloud_off_rounded,
                size: 72,
                color: AppColors.outline,
              ),
            const SizedBox(height: 16),
            Text(
              error.isNotEmpty
                  ? error
                  : 'Photo uploads here automatically\nafter every door-close scan.',
              textAlign: TextAlign.center,
              style: TextStyle(
                color: error.isNotEmpty
                    ? AppColors.error
                    : AppColors.onSurfaceVariant,
                fontSize: 14,
                height: 1.5,
              ),
            ),
          ],
        ),
      ),
    );
  }
}

// ── Insight card ──────────────────────────────────────────────────────────────
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
                        fontSize: 11, color: AppColors.onSurfaceVariant)),
                Text(value,
                    style: TextStyle(
                        fontSize: 18,
                        fontWeight: FontWeight.w800,
                        color: color)),
                Text(subtitle,
                    style: const TextStyle(
                        fontSize: 10, color: AppColors.onSurfaceVariant)),
              ],
            ),
          ),
        ],
      ),
    );
  }
}
