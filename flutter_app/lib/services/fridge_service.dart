import 'dart:typed_data';
import 'package:cloud_firestore/cloud_firestore.dart';
import 'package:firebase_database/firebase_database.dart';
import 'package:firebase_core/firebase_core.dart';
import '../config.dart';
import '../models/fridge_item.dart';
import '../models/scan_record.dart';

class ShoppingItem {
  final String id; // Firestore document ID
  final String name;
  final bool checked;

  const ShoppingItem({required this.id, required this.name, this.checked = false});

  factory ShoppingItem.fromDoc(String id, Map<String, dynamic> map) => ShoppingItem(
        id: id,
        name: map['name'] as String? ?? '',
        checked: map['checked'] as bool? ?? false,
      );
}

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
  // Each call opens its own Firestore listener (with an immediate initial
  // snapshot). Screens must hold the returned stream in a State field —
  // calling these inside build() re-subscribes on every rebuild.

  /// Live inventory — rebuilds UI instantly on every ESP32-CAM scan.
  static Stream<InventorySnapshot?> inventoryStream() => _fridgeRef
      .collection('inventory')
      .doc('current')
      .snapshots()
      .map((s) => s.exists && s.data() != null
          ? InventorySnapshot.fromMap(s.data()!)
          : null);

  /// Live door state from hall sensor on the CH board.
  static Stream<DoorStatus?> doorStream() => _fridgeRef
      .collection('sensors')
      .doc('door')
      .snapshots()
      .map((s) => s.exists && s.data() != null
          ? DoorStatus.fromMap(s.data()!)
          : null);

  /// Live temperature/humidity from DHT11 on the CAM roof1 board.
  static Stream<TemperatureReading?> temperatureStream() => _fridgeRef
      .collection('sensors')
      .doc('temperature')
      .snapshots()
      .map((s) => s.exists && s.data() != null
          ? TemperatureReading.fromMap(s.data()!)
          : null);

  /// Last 20 scans, newest first. Ordered by the `timestamp` field the
  /// firmware writes ("YYYY-MM-DD HH:MM:SS", lexicographically
  /// chronological) — without an orderBy, Firestore returns the 20 OLDEST
  /// docs, and descending doc-ID order would need a composite index.
  static Stream<List<ScanRecord>> scanHistoryStream() => _fridgeRef
      .collection('scans')
      .orderBy('timestamp', descending: true)
      .limit(20)
      .snapshots()
      .map((s) =>
          s.docs.map((d) => ScanRecord.fromDoc(d.id, d.data())).toList());

  /// True total number of scans ever taken (server-side count aggregate,
  /// not capped by the 20-scan history window).
  static Future<int?> totalScanCount() async {
    try {
      final agg = await _fridgeRef.collection('scans').count().get();
      return agg.count;
    } catch (_) {
      return null;
    }
  }

  /// Purchase counters the CAM firmware maintains in
  /// fridges/{id}/bought/{YYYY-MM}: field name = item name, value = times
  /// bought this month. Empty map if the doc doesn't exist yet.
  static Stream<Map<String, int>> boughtCountsStream() {
    final now = DateTime.now();
    final monthId = '${now.year}-${now.month.toString().padLeft(2, '0')}';
    return _fridgeRef.collection('bought').doc(monthId).snapshots().map((s) {
      final data = s.data();
      if (data == null) return <String, int>{};
      return data.map((k, v) => MapEntry(k, (v as num?)?.toInt() ?? 0));
    });
  }

  // ── Live View (RTDB request → CAM board → Firestore photo) ─────────────────

  static Future<void> requestLiveViewSnapshot(int roof) {
    return _rtdb
        .ref('fridges/${AppConfig.fridgeId}/liveview_requests/roof$roof/requested_at')
        .set(ServerValue.timestamp);
  }

  /// capturedAt of the photo currently stored for this roof (null if none).
  /// Screens snapshot this before requesting so they can tell a fresh photo
  /// from the stale one already in Firestore.
  static Future<String?> currentLiveViewCapturedAt(int roof) async {
    try {
      final doc =
          await _fridgeRef.collection('liveview').doc('roof$roof').get();
      return doc.data()?['capturedAt'] as String?;
    } catch (_) {
      return null;
    }
  }

  /// Live photo for the given roof (1-based). The CAM board writes a
  /// base64-encoded JPEG to `photo` (Firestore bytesValue) and a timestamp
  /// string to `capturedAt`.
  static Stream<({Uint8List? photo, String? capturedAt})> liveViewPhotoStream(
          int roof) =>
      _fridgeRef.collection('liveview').doc('roof$roof').snapshots().map(
            (s) => (
              photo: (s.data()?['photo'] as Blob?)?.bytes,
              capturedAt: s.data()?['capturedAt'] as String?,
            ),
          );

  // ── Shopping List (fridges/{id}/shopping_list/{auto-id}) ───────────────────

  static CollectionReference get _shoppingRef =>
      _fridgeRef.collection('shopping_list');

  static Stream<List<ShoppingItem>> shoppingListStream() => _shoppingRef
      .orderBy('name')
      .snapshots()
      .map((s) => s.docs
          .map((d) => ShoppingItem.fromDoc(d.id, d.data() as Map<String, dynamic>))
          .toList());

  static Future<void> addShoppingItem(String name) async {
    final key = name.toLowerCase().trim();
    // Avoid duplicate entries (same name already in list)
    final existing = await _shoppingRef.where('name', isEqualTo: key).limit(1).get();
    if (existing.docs.isNotEmpty) return;
    await _shoppingRef.add({'name': key, 'checked': false});
  }

  static Future<void> toggleShoppingItem(String id, bool currentChecked) =>
      _shoppingRef.doc(id).update({'checked': !currentChecked});

  static Future<void> removeShoppingItem(String id) =>
      _shoppingRef.doc(id).delete();

  static Future<void> clearShoppingList() async {
    final batch = _db.batch();
    final docs = await _shoppingRef.get();
    for (final doc in docs.docs) {
      batch.delete(doc.reference);
    }
    await batch.commit();
  }

  // ── Utilities ─────────────────────────────────────────────────────────────

  /// True when a liveview capturedAt ("YYYY-MM-DD HH:MM:SS", ESP32 local
  /// time) is recent enough to present as live rather than a stored snapshot.
  static bool isRecentCapture(String? capturedAt,
      {Duration within = const Duration(minutes: 2)}) {
    if (capturedAt == null) return false;
    final t = DateTime.tryParse(capturedAt.replaceFirst(' ', 'T'));
    if (t == null) return false;
    return DateTime.now().difference(t) < within;
  }

  /// Firebase Storage URL for an item icon. The app generates and uploads
  /// PNGs; the CH display board uses a separate JPEG set (icons/{name}.jpg)
  /// — ItemIcon tries both before generating a new one.
  static String iconUrl(String itemName, {String extension = 'png'}) {
    final key = itemName.toLowerCase().trim();
    final encoded = Uri.encodeComponent('icons/$key.$extension');
    return 'https://firebasestorage.googleapis.com/v0/b'
        '/${AppConfig.storageBucket}/o/$encoded?alt=media';
  }
}
