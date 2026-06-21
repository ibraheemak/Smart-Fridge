import 'package:cloud_firestore/cloud_firestore.dart';
import '../config.dart';
import '../models/fridge_item.dart';
import '../models/scan_record.dart';

class FridgeService {
  static final _db = FirebaseFirestore.instance;
  static DocumentReference get _fridgeRef =>
      _db.collection('fridges').doc(AppConfig.fridgeId);

  // ── Real-time streams ──────────────────────────────────────────────────────

  /// Live inventory — rebuilds UI instantly on every ESP32-CAM scan.
  static Stream<InventorySnapshot?> inventoryStream() => _fridgeRef
      .collection('inventory')
      .doc('current')
      .snapshots()
      .map((s) => s.exists && s.data() != null
          ? InventorySnapshot.fromMap(s.data()!)
          : null);

  /// Live temperature/humidity from DHT11 on the CH board.
  static Stream<TemperatureReading?> temperatureStream() => _fridgeRef
      .collection('sensors')
      .doc('temperature')
      .snapshots()
      .map((s) => s.exists && s.data() != null
          ? TemperatureReading.fromMap(s.data()!)
          : null);

  /// Last 20 scans, newest first.
  /// Sorted client-side (doc IDs are ISO timestamps — sort lexicographically).
  static Stream<List<ScanRecord>> scanHistoryStream() => _fridgeRef
      .collection('scans')
      .limit(20)
      .snapshots()
      .map((s) {
        final docs = s.docs
            .map((d) => ScanRecord.fromDoc(d.id, d.data()))
            .toList()
          ..sort((a, b) => b.id.compareTo(a.id));
        return docs;
      });

  // ── One-shot reads ─────────────────────────────────────────────────────────

  /// Canonical item names from basic-items/basic-items.
  /// The ESP32 uses these to steer Gemini toward known product names.
  static Future<List<String>> fetchBasicItems() async {
    try {
      final doc =
          await _db.collection('basic-items').doc('basic-items').get();
      if (!doc.exists) return [];
      final raw = doc.data()?['items'];
      if (raw is List) return raw.cast<String>();
      if (raw is String) {
        return raw.split(',').map((s) => s.trim()).toList();
      }
      return [];
    } catch (_) {
      return [];
    }
  }

  // ── Utilities ─────────────────────────────────────────────────────────────

  /// Firebase Storage URL for item icon: icons/{name}.jpg
  static String iconUrl(String itemName) {
    final encoded = Uri.encodeComponent('icons/$itemName.jpg');
    return 'https://firebasestorage.googleapis.com/v0/b'
        '/${AppConfig.storageBucket}/o/$encoded?alt=media';
  }
}
