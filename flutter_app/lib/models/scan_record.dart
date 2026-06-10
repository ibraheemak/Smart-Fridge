import 'fridge_item.dart';

/// One scan event from fridges/{id}/scans/{isoTimestamp}
class ScanRecord {
  final String id;        // Firestore document ID = ISO timestamp
  final String timestamp; // "2026-06-10 14:35:22"
  final String weekId;    // "2026-06-W2"
  final String monthId;   // "2026-06"
  final String source;
  final List<FridgeItem> items;

  const ScanRecord({
    required this.id,
    required this.timestamp,
    required this.weekId,
    required this.monthId,
    required this.source,
    required this.items,
  });

  factory ScanRecord.fromDoc(String id, Map<String, dynamic> map) {
    final raw = map['items'] as List<dynamic>? ?? [];
    return ScanRecord(
      id: id,
      timestamp: map['timestamp'] as String? ?? '',
      weekId: map['weekId'] as String? ?? '',
      monthId: map['monthId'] as String? ?? '',
      source: map['source'] as String? ?? '',
      items: raw.map((e) => FridgeItem.fromMap(e as Map<String, dynamic>)).toList(),
    );
  }

  int get itemCount => items.length;
}
