import 'dart:typed_data';
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
  Uint8List? _imageBytes;
  String _camUrl = AppConfig.esp32CamBaseUrl;
  bool _loading = false;
  bool _scanning = false;
  String _error = '';
  DateTime? _fetchedAt;

  Future<void> _refresh() async {
    if (_loading) return;
    setState(() {
      _loading = true;
      _error = '';
    });
    try {
      final res = await http
          .get(Uri.parse('$_camUrl/latest.jpg'))
          .timeout(const Duration(seconds: 10));
      if (res.statusCode == 200) {
        setState(() {
          _imageBytes = res.bodyBytes;
          _fetchedAt = DateTime.now();
          _error = '';
        });
      } else {
        setState(
            () => _error = 'No image yet — close the door to trigger a scan.');
      }
    } catch (_) {
      setState(() => _error =
          'Cannot reach ESP32-CAM.\n'
          '• Check that your phone is on the same WiFi as the fridge.\n'
          '• Tap the settings icon to update the IP address.\n'
          '• Current: $_camUrl');
    } finally {
      if (mounted) setState(() => _loading = false);
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
            content: Text('AI scan triggered! Updating inventory...'),
            backgroundColor: AppColors.secondary,
          ),
        );
        // Reload image after scan
        await Future.delayed(const Duration(seconds: 3));
        await _refresh();
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
              'Find the IP in the Arduino Serial Monitor on boot:\n[WEB] http://192.168.x.x/latest.jpg',
              style: TextStyle(
                  fontSize: 12, color: AppColors.onSurfaceVariant),
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
              _refresh();
            },
            child: const Text('Connect'),
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
              // ── Header ─────────────────────────────────────────────────
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
                  IconButton(
                    onPressed: _loading ? null : _refresh,
                    icon: _loading
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
                _camUrl,
                style: const TextStyle(
                    fontSize: 12, color: AppColors.onSurfaceVariant),
              ),
              const SizedBox(height: 16),

              // ── Image frame ─────────────────────────────────────────────
              Expanded(
                child: ClipRRect(
                  borderRadius: BorderRadius.circular(20),
                  child: Stack(
                    fit: StackFit.expand,
                    children: [
                      // Image / placeholder
                      Container(
                        color: AppColors.surfaceContainerHighest,
                        child: _imageBytes != null
                            ? InteractiveViewer(
                                child: Image.memory(_imageBytes!,
                                    fit: BoxFit.contain))
                            : Center(
                                child: Padding(
                                  padding: const EdgeInsets.all(32),
                                  child: Column(
                                    mainAxisSize: MainAxisSize.min,
                                    children: [
                                      Icon(
                                        Icons.videocam_rounded,
                                        size: 72,
                                        color: AppColors.outline,
                                      ),
                                      const SizedBox(height: 16),
                                      Text(
                                        _error.isEmpty
                                            ? 'Tap the button below to\nload the latest fridge photo'
                                            : _error,
                                        textAlign: TextAlign.center,
                                        style: TextStyle(
                                          color: _error.isEmpty
                                              ? AppColors.onSurfaceVariant
                                              : AppColors.error,
                                          fontSize: 14,
                                          height: 1.5,
                                        ),
                                      ),
                                    ],
                                  ),
                                ),
                              ),
                      ),

                      // LIVE badge (when image is loaded)
                      if (_imageBytes != null)
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
                ),
              ),

              const SizedBox(height: 12),

              // Timestamp
              if (_fetchedAt != null)
                Center(
                  child: Text(
                    'Loaded at ${_time(_fetchedAt!)}  •  tap refresh to update',
                    style: const TextStyle(
                        fontSize: 11, color: AppColors.onSurfaceVariant),
                  ),
                ),

              const SizedBox(height: 14),

              // Buttons row
              Row(
                children: [
                  Expanded(
                    child: ElevatedButton.icon(
                      onPressed: _loading ? null : _refresh,
                      icon: _loading
                          ? const SizedBox(
                              width: 18,
                              height: 18,
                              child: CircularProgressIndicator(
                                  strokeWidth: 2, color: Colors.white))
                          : const Icon(Icons.refresh_rounded),
                      label: Text(
                          _imageBytes == null ? 'Load Photo' : 'Refresh'),
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

  String _time(DateTime dt) =>
      '${dt.hour.toString().padLeft(2, '0')}:'
      '${dt.minute.toString().padLeft(2, '0')}:'
      '${dt.second.toString().padLeft(2, '0')}';
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
