import 'dart:convert';
import 'dart:typed_data';
import 'package:firebase_storage/firebase_storage.dart';
import 'package:http/http.dart' as http;
import '../config.dart';

/// Generates food icons via Gemini image generation and stores them in
/// Firebase Storage so every device gets the exact same image.
///
/// Style is locked to "flat vector icon with bold black outline on white"
/// so all items look like they belong to the same icon pack.
class IconGeneratorService {
  // Uses Gemini image generation — model set in AppConfig.geminiImageEndpoint

  // All icons in the same flat-vector style pack
  static const _prompt =
      'Create a simple flat vector icon of "{item}" for a smart fridge app. '
      'Pure white background, bold black outline, flat solid colors (3–4 colors max), '
      'NO gradients, NO shadows, NO text, NO labels, NO realistic shading. '
      'Centered square composition. Style: consistent flat icon pack, like Google Material icons but for food.';

  static final _storage = FirebaseStorage.instance;
  static final _cache = <String, Uint8List?>{};
  static final _inFlight = <String, Future<Uint8List?>>{};

  static String _key(String itemName) => itemName.toLowerCase().trim();

  /// Returns PNG bytes for [itemName], generating and uploading if needed.
  /// Concurrent calls for the same item share one in-flight request.
  static Future<Uint8List?> generateIcon(String itemName) {
    final key = _key(itemName);
    if (_cache.containsKey(key)) return Future.value(_cache[key]);
    return _inFlight.putIfAbsent(key, () {
      final f = _fetch(key, itemName);
      f.then((v) {
        _cache[key] = v;
        _inFlight.remove(key);
      });
      return f;
    });
  }

  /// Pre-generate icons for a list of item names (call from Settings).
  static Future<void> regenerateAll(List<String> itemNames) async {
    _cache.clear();
    for (final name in itemNames) {
      await generateIcon(name);
    }
  }

  // ── Private ────────────────────────────────────────────────────────────────

  static Future<Uint8List?> _fetch(String key, String itemName) async {
    try {
      final prompt = _prompt.replaceAll('{item}', itemName);
      final body = jsonEncode({
        'contents': [
          {
            'parts': [{'text': prompt}]
          }
        ],
        'generationConfig': {
          'responseModalities': ['TEXT', 'IMAGE'],
        },
      });

      final res = await http
          .post(
            Uri.parse(AppConfig.geminiImageEndpoint),
            headers: {
              'Content-Type': 'application/json',
              'X-goog-api-key': AppConfig.geminiApiKey,
            },
            body: body,
          )
          .timeout(const Duration(seconds: 90));

      if (res.statusCode == 200) {
        final raw = jsonDecode(res.body) as Map<String, dynamic>;
        final parts =
            raw['candidates']?[0]?['content']?['parts'] as List?;
        for (final part in parts ?? []) {
          final inline = part['inlineData'];
          if (inline != null) {
            final bytes = base64Decode(inline['data'] as String);
            _uploadToStorage(key, bytes);
            return bytes;
          }
        }
      }
    } catch (_) {}
    return null;
  }

  /// Uploads generated PNG to icons/{key}.png in Firebase Storage.
  /// All devices will then load the same image via CachedNetworkImage.
  static Future<void> _uploadToStorage(String key, Uint8List bytes) async {
    try {
      final ref = _storage.ref('icons/$key.png');
      await ref.putData(
        bytes,
        SettableMetadata(contentType: 'image/png'),
      );
    } catch (_) {
      // Non-fatal — image is already shown from memory cache
    }
  }
}
