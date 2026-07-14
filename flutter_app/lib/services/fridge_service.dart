import 'dart:typed_data';
import 'package:cloud_firestore/cloud_firestore.dart';
import 'package:firebase_database/firebase_database.dart';
import 'package:firebase_core/firebase_core.dart';
import '../config.dart';
import '../models/fridge_item.dart';
import '../models/scan_record.dart';

class FridgeService {
  static final _db = FirebaseFirestore.instance;
  static DocumentReference get _fridgeRef =>
      _db.collection('fridges').doc(AppConfig.fridgeId);

  // firebase_options.dart doesn't set databaseURL, so the default
  // FirebaseDatabase.instance can't find the RTDB instance — point it at the
  // URL explicitly (same instance as ESP32 SECRETS.h's FIREBASE_DATABASE_URL).
  static final _rtdb = FirebaseDatabase.instanceFor(
    app: Firebase.app(),
    databaseURL: AppConfig.rtdbUrl,
  );

  // ── Real-time streams ──────────────────────────────────────────────────────

  /// Live inventory — rebuilds UI instantly on every ESP32-CAM scan.
  static Stream<InventorySnapshot?> inventoryStream() => _fridgeRef
      .collection('inventory')
      .doc('current')
      .snapshots()
      .map((s) => s.exists && s.data() != null
          ? InventorySnapshot.fromMap(s.data()!)
          : null);

  /// Live door state from hall sensor on the CH board.
  /// Document: fridges/{id}/sensors/door  fields: state, updatedAt
  static Stream<DoorStatus?> doorStream() => _fridgeRef
      .collection('sensors')
      .doc('door')
      .snapshots()
      .map((s) => s.exists && s.data() != null
          ? DoorStatus.fromMap(s.data()!)
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

  // ── Live View (RTDB request → CAM board → Firestore photo) ─────────────────

  /// Asks the given roof's ESP32-CAM board (1-based) for a fresh snapshot.
  /// The CAM board keeps an RTDB SSE stream open on this exact path (see
  /// liveview_rtdb.h) and reacts to any change here — same "bump a
  /// server-timestamp value" idiom the boards already use for the inventory
  /// doorbell (rtdb_notify.h).
  static Future<void> requestLiveViewSnapshot(int roof) {
    return _rtdb
        .ref('fridges/${AppConfig.fridgeId}/liveview_requests/roof$roof/requested_at')
        .set(ServerValue.timestamp);
  }

  /// Live Live View photo for the given roof (1-based). Firestore's
  /// `bytesValue` field type comes back from cloud_firestore as a [Blob]
  /// wrapper, not a raw Uint8List — .bytes unwraps it.
  static Stream<({Uint8List? photo, String? capturedAt})> liveViewPhotoStream(
          int roof) =>
      _fridgeRef.collection('liveview').doc('roof$roof').snapshots().map(
            (s) => (
              photo: (s.data()?['photo'] as Blob?)?.bytes,
              capturedAt: s.data()?['capturedAt'] as String?,
            ),
          );

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

  /// Firebase Storage URL for item icon: icons/{name}.png
  static String iconUrl(String itemName) {
    final key = itemName.toLowerCase().trim();
    final encoded = Uri.encodeComponent('icons/$key.png');
    return 'https://firebasestorage.googleapis.com/v0/b'
        '/${AppConfig.storageBucket}/o/$encoded?alt=media';
  }
}
