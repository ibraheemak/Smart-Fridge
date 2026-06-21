import 'dart:convert';
import 'dart:typed_data';
import 'package:http/http.dart' as http;
import '../config.dart';

/// Generates food icons via Gemini image generation.
/// Results are cached in memory for the session lifetime.
class IconGeneratorService {
  static const _model = 'gemini-2.0-flash-preview-image-generation';
  static final _cache = <String, Uint8List?>{};
  static final _inFlight = <String, Future<Uint8List?>>{};

  /// Returns a PNG as bytes for [itemName], or null on failure.
  /// Concurrent calls for the same item share one in-flight request.
  static Future<Uint8List?> generateIcon(String itemName) {
    final key = itemName.toLowerCase().trim();
    if (_cache.containsKey(key)) return Future.value(_cache[key]);
    return _inFlight.putIfAbsent(key, () => _fetch(key, itemName)
      ..then((v) {
        _cache[key] = v;
        _inFlight.remove(key);
      }));
  }

  static Future<Uint8List?> _fetch(String key, String itemName) async {
    try {
      final body = jsonEncode({
        'contents': [
          {
            'parts': [
              {
                'text':
                    'Create a clean, simple food product icon of "$itemName" '
                    'for a smart fridge mobile app. White background, flat cartoon '
                    'design, vibrant saturated colors, no text or labels, '
                    'centered square composition, app icon style.'
              }
            ]
          }
        ],
        'generationConfig': {
          'responseModalities': ['IMAGE'],
        }
      });

      final res = await http
          .post(
            Uri.parse(
                'https://generativelanguage.googleapis.com/v1beta'
                '/models/$_model:generateContent?key=${AppConfig.geminiApiKey}'),
            headers: {'Content-Type': 'application/json'},
            body: body,
          )
          .timeout(const Duration(seconds: 30));

      if (res.statusCode == 200) {
        final raw = jsonDecode(res.body) as Map<String, dynamic>;
        final parts = raw['candidates']?[0]?['content']?['parts'] as List?;
        for (final part in parts ?? []) {
          final inline = part['inlineData'];
          if (inline != null) {
            return base64Decode(inline['data'] as String);
          }
        }
      }
    } catch (_) {}
    return null;
  }

  static bool isCached(String itemName) =>
      _cache.containsKey(itemName.toLowerCase().trim());
}
