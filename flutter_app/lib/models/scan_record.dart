import 'fridge_item.dart';

/// One scan event from fridges/{id}/scans/{isoTimestamp}
class ScanRecord {
  final String id;        // Firestore document ID = "YYYY-MM-DDTHH-MM-SS"
  final String timestamp; // "2026-06-10 14:35:22"
  final String source;
  final List<FridgeItem> items;

  const ScanRecord({
    required this.id,
    required this.timestamp,
    required this.source,
    required this.items,
  });

  factory ScanRecord.fromDoc(String id, Map<String, dynamic> map) {
    final raw = map['items'] as List<dynamic>? ?? [];
    return ScanRecord(
      id: id,
      timestamp: map['timestamp'] as String? ?? '',
      source: map['source'] as String? ?? '',
      items: raw.map((e) => FridgeItem.fromMap(e as Map<String, dynamic>)).toList(),
    );
  }

  int get itemCount => items.length;

  /// Parsed scan time. The doc ID uses hyphens in the time part
  /// ("14-35-22") which DateTime.parse rejects — use the timestamp
  /// field the firmware also writes ("YYYY-MM-DD HH:MM:SS").
  DateTime? get parsedTimestamp =>
      DateTime.tryParse(timestamp.replaceFirst(' ', 'T'));
}
