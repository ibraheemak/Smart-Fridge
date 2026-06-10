import 'dart:typed_data';
import 'package:flutter/material.dart';
import 'package:http/http.dart' as http;
import '../config.dart';
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
        });
      } else {
        setState(() => _error =
            'No image yet — send a SCAN command first, or close the door.');
      }
    } catch (_) {
      setState(() => _error =
          'Cannot reach ESP32-CAM.\n'
          '• Check that your phone is on the same WiFi as the fridge.\n'
          '• Tap ⚙ to update the IP address.\n'
          '• Current IP: $_camUrl');
    } finally {
      setState(() => _loading = false);
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
            const Text(
              'Find the IP in the Arduino Serial Monitor on boot:\n[WEB] http://192.168.x.x/latest.jpg',
              style: TextStyle(
                  fontSize: 12, color: AppColors.textSecondary),
            ),
            const SizedBox(height: 12),
            TextField(
              controller: ctrl,
              keyboardType: TextInputType.url,
              decoration: const InputDecoration(
                hintText: 'http://192.168.1.100',
                prefixIcon: Icon(Icons.wifi_rounded),
              ),
            ),
          ],
        ),
        actions: [
          TextButton(
              onPressed: () => Navigator.pop(context),
              child: const Text('Cancel')),
          FilledButton(
            style: FilledButton.styleFrom(
                backgroundColor: AppColors.primary),
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
      body: SafeArea(
        child: Padding(
          padding: const EdgeInsets.all(20),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              // Header
              Row(
                children: [
                  Text('Camera',
                      style: Theme.of(context).textTheme.headlineMedium),
                  const Spacer(),
                  IconButton(
                    onPressed: _showIpDialog,
                    icon: const Icon(Icons.settings_rounded),
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
                        : const Icon(Icons.refresh_rounded),
                    tooltip: 'Refresh',
                  ),
                ],
              ),
              Text(
                _camUrl,
                style: const TextStyle(
                    fontSize: 12, color: AppColors.textSecondary),
              ),
              const SizedBox(height: 14),

              // Image frame
              Expanded(
                child: Container(
                  width: double.infinity,
                  clipBehavior: Clip.antiAlias,
                  decoration: BoxDecoration(
                    color: AppColors.card,
                    borderRadius: BorderRadius.circular(20),
                    boxShadow: const [
                      BoxShadow(
                        color: AppColors.shadow,
                        blurRadius: 20,
                        offset: Offset(0, 6),
                      ),
                    ],
                  ),
                  child: _buildImage(),
                ),
              ),

              const SizedBox(height: 12),

              // Timestamp
              if (_fetchedAt != null)
                Center(
                  child: Text(
                    'Loaded at ${_time(_fetchedAt!)}  •  tap 🔄 to refresh',
                    style: const TextStyle(
                        fontSize: 11, color: AppColors.textSecondary),
                  ),
                ),

              const SizedBox(height: 14),

              // Big refresh button
              SizedBox(
                width: double.infinity,
                child: FilledButton.icon(
                  onPressed: _loading ? null : _refresh,
                  icon: const Icon(Icons.camera_alt_rounded),
                  label: Text(
                      _imageBytes == null ? 'Load Fridge Photo' : 'Refresh'),
                  style: FilledButton.styleFrom(
                    backgroundColor: AppColors.primary,
                    padding: const EdgeInsets.symmetric(vertical: 14),
                    shape: RoundedRectangleBorder(
                        borderRadius: BorderRadius.circular(14)),
                  ),
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }

  Widget _buildImage() {
    if (_imageBytes != null) {
      return InteractiveViewer(
        child: Image.memory(_imageBytes!, fit: BoxFit.contain),
      );
    }
    return Center(
      child: Padding(
        padding: const EdgeInsets.all(32),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Icon(
              Icons.camera_alt_rounded,
              size: 72,
              color: AppColors.textHint,
            ),
            const SizedBox(height: 16),
            Text(
              _error.isEmpty ? 'Tap the button to load the\nlatest fridge photo' : _error,
              textAlign: TextAlign.center,
              style: TextStyle(
                color: _error.isEmpty
                    ? AppColors.textSecondary
                    : AppColors.error,
                fontSize: 13,
                height: 1.5,
              ),
            ),
          ],
        ),
      ),
    );
  }

  String _time(DateTime dt) =>
      '${dt.hour.toString().padLeft(2, '0')}:'
      '${dt.minute.toString().padLeft(2, '0')}:'
      '${dt.second.toString().padLeft(2, '0')}';
}
