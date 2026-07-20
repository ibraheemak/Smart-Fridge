import 'dart:convert';
import 'package:http/http.dart' as http;

/// Free product photos from Open Food Facts — no API key, no quota.
///
/// Only the **barcode** endpoint is used: OFF's name-search backend is
/// frequently overloaded (502/503), but `api/v2/product/{barcode}.json` is
/// reliable. The GM65 scanner already resolves a product by barcode to get
/// its name; storing that barcode lets the app fetch the matching photo.
class OpenFoodFactsService {
  static const _ua = 'SmartFridge-Group8/1.0 (Technion ICST project)';

  // Small in-memory cache so the same barcode isn't fetched repeatedly.
  static final _cache = <String, String?>{};
  static final _inFlight = <String, Future<String?>>{};

  /// Front product image URL for a barcode, or null if the product/photo
  /// isn't found. Prefers the small (200px) image to save bandwidth.
  static Future<String?> imageForBarcode(String barcode) {
    final code = barcode.trim();
    if (code.isEmpty) return Future.value(null);
    if (_cache.containsKey(code)) return Future.value(_cache[code]);
    return _inFlight.putIfAbsent(code, () {
      final f = _fetch(code);
      f.then((v) {
        _cache[code] = v;
        _inFlight.remove(code);
      });
      return f;
    });
  }

  static Future<String?> _fetch(String code) async {
    try {
      final uri = Uri.parse(
          'https://world.openfoodfacts.org/api/v2/product/$code.json'
          '?fields=image_front_small_url,image_front_url,image_url');
      final res = await http
          .get(uri, headers: {'User-Agent': _ua})
          .timeout(const Duration(seconds: 8));
      if (res.statusCode != 200) return null;
      final data = jsonDecode(res.body) as Map<String, dynamic>;
      if (data['status'] != 1) return null; // 1 = product found
      final p = data['product'] as Map<String, dynamic>?;
      if (p == null) return null;
      final url = (p['image_front_small_url'] ??
          p['image_front_url'] ??
          p['image_url']) as String?;
      return (url != null && url.isNotEmpty) ? url : null;
    } catch (_) {
      return null;
    }
  }
}
