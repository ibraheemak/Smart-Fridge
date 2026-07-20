import 'dart:convert';
import 'dart:typed_data';
import 'package:firebase_storage/firebase_storage.dart';
import 'package:http/http.dart' as http;
import 'package:image/image.dart' as img;
import '../config.dart';

/// Generates food icons via Gemini image generation and stores them in
/// Firebase Storage so every device gets the exact same image.
///
/// Style is locked to "flat vector icon with bold black outline on white"
/// so all items look like they belong to the same icon pack.
///
/// Every icon is normalized to the fridge-icon spec before upload:
/// **350×350 px, 72 DPI, baseline (non-progressive) JPEG** — the format the
/// ESP32 CH display board's JPEG decoder requires (it can't decode PNG or
/// progressive JPEG). Stored at `icons/{key}.jpg`.
class IconGeneratorService {
  // Uses Gemini image generation — model set in AppConfig.geminiImageEndpoint

  /// Fridge-icon output spec (also documented on the ESP32 CH board).
  static const iconPx = 350;
  static const iconDpi = 72;
  static const iconJpegQuality = 90;

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

  /// Normalize any decodable image (PNG/JPEG/…) to the fridge-icon spec:
  /// square-cropped to 350×350, baseline JPEG, 72 DPI. Returns null if the
  /// input can't be decoded. Pure/synchronous — unit-testable in isolation.
  static Uint8List? toFridgeIcon(Uint8List src) {
    final decoded = img.decodeImage(src);
    if (decoded == null) return null;
    // Center-crop to a square then resize to exactly 350×350 (no distortion).
    final square = img.copyResizeCropSquare(decoded, size: iconPx);
    // encodeJpg always emits BASELINE (non-progressive) JPEG.
    final jpeg = img.encodeJpg(square, quality: iconJpegQuality);
    return _setJfifDpi(jpeg, iconDpi);
  }

  /// Rewrite the JFIF APP0 density to [dpi]×[dpi] dots-per-inch. Leaves the
  /// bytes untouched if the expected JFIF header isn't found (still valid
  /// baseline JPEG; DPI is cosmetic for a pixel display).
  static Uint8List _setJfifDpi(Uint8List b, int dpi) {
    // SOI (FFD8) + APP0 (FFE0) + len(2) + "JFIF\0" + version(2) + units +
    // Xdensity(2) + Ydensity(2). units at [13], densities at [14..17].
    if (b.length >= 18 &&
        b[2] == 0xFF && b[3] == 0xE0 &&
        b[6] == 0x4A && b[7] == 0x46 && b[8] == 0x49 && b[9] == 0x46 &&
        b[10] == 0x00) {
      b[13] = 0x01; // units: dots per inch
      b[14] = (dpi >> 8) & 0xFF;
      b[15] = dpi & 0xFF;
      b[16] = (dpi >> 8) & 0xFF;
      b[17] = dpi & 0xFF;
    }
    return b;
  }

  /// Returns baseline-JPEG icon bytes for [itemName], generating and
  /// uploading if needed. Concurrent calls for the same item share one
  /// in-flight request.
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
            final raw = base64Decode(inline['data'] as String);
            // Normalize to 350×350 baseline JPEG @ 72 DPI before storing.
            final icon = toFridgeIcon(raw) ?? raw;
            _uploadToStorage(key, icon);
            return icon;
          }
        }
      }
    } catch (_) {}
    return null;
  }

  /// Uploads the normalized icon to icons/{key}.jpg (baseline JPEG) in
  /// Firebase Storage — the exact object the ESP32 CH board fetches and every
  /// app instance loads via CachedNetworkImage.
  static Future<void> _uploadToStorage(String key, Uint8List bytes) async {
    try {
      final ref = _storage.ref('icons/$key.jpg');
      await ref.putData(
        bytes,
        SettableMetadata(contentType: 'image/jpeg'),
      );
    } catch (_) {
      // Non-fatal — image is already shown from memory cache
    }
  }
}
