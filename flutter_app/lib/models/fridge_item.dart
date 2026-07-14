import 'package:cloud_firestore/cloud_firestore.dart';

// Parses either a Firestore Timestamp or an ESP32 string ("YYYY-MM-DD HH:MM:SS").
DateTime _parseTimestamp(dynamic v) {
  if (v == null) return DateTime.now();
  if (v is Timestamp) return v.toDate();
  if (v is String) {
    return DateTime.tryParse(v.replaceFirst(' ', 'T')) ?? DateTime.now();
  }
  return DateTime.now();
}

enum ExpiryStatus { expired, critical, soon, ok, unknown }

class FridgeItem {
  final String name;
  final String quantity;
  final String confidence;
  final List<String> expiries; // "YYYY-MM-DD" strings set by CH touch UI

  const FridgeItem({
    required this.name,
    required this.quantity,
    required this.confidence,
    this.expiries = const [],
  });

  factory FridgeItem.fromMap(Map<String, dynamic> map) {
    final rawExpiries = map['expiries'] as List<dynamic>? ?? [];
    return FridgeItem(
      name: map['name'] as String? ?? '',
      quantity: map['quantity'] as String? ?? '',
      confidence: map['confidence'] as String? ?? 'low',
      expiries: rawExpiries.whereType<String>().toList(),
    );
  }

  bool get isHighConfidence => confidence == 'high';
  bool get isMediumConfidence => confidence == 'medium';

  String get displayName =>
      name.isNotEmpty ? name[0].toUpperCase() + name.substring(1) : '';

  // Nearest expiry date (parsed). Returns null if no dates or all malformed.
  DateTime? get nextExpiry {
    DateTime? nearest;
    for (final s in expiries) {
      final d = DateTime.tryParse(s);
      if (d == null) continue;
      if (nearest == null || d.isBefore(nearest)) nearest = d;
    }
    return nearest;
  }

  ExpiryStatus get expiryStatus {
    final next = nextExpiry;
    if (next == null) return ExpiryStatus.unknown;
    final days = next.difference(DateTime.now()).inDays;
    if (days < 0) return ExpiryStatus.expired;
    if (days <= 3) return ExpiryStatus.critical;
    if (days <= 7) return ExpiryStatus.soon;
    return ExpiryStatus.ok;
  }

  // Human-readable label for the nearest expiry date.
  String get expiryLabel {
    final next = nextExpiry;
    if (next == null) return 'No date set';
    final days = next.difference(DateTime.now()).inDays;
    if (days < 0) return 'Expired ${-days}d ago';
    if (days == 0) return 'Expires today!';
    if (days == 1) return 'Expires tomorrow!';
    return 'Expires in ${days}d';
  }
}

/// Current inventory state from fridges/{id}/inventory/current
class InventorySnapshot {
  final List<FridgeItem> items;
  final DateTime updatedAt;
  final String source;

  const InventorySnapshot({
    required this.items,
    required this.updatedAt,
    required this.source,
  });

  factory InventorySnapshot.fromMap(Map<String, dynamic> map) {
    final raw = map['items'] as List<dynamic>? ?? [];
    return InventorySnapshot(
      items: raw.map((e) => FridgeItem.fromMap(e as Map<String, dynamic>)).toList(),
      updatedAt: _parseTimestamp(map['updatedAt']),
      source: map['source'] as String? ?? '',
    );
  }
}

/// Temperature/humidity from fridges/{id}/sensors/temperature
class TemperatureReading {
  final double? temperatureC;
  final double? humidity;
  final DateTime updatedAt;

  const TemperatureReading({
    this.temperatureC,
    this.humidity,
    required this.updatedAt,
  });

  factory TemperatureReading.fromMap(Map<String, dynamic> map) =>
      TemperatureReading(
        temperatureC: (map['temperature'] as num?)?.toDouble(),
        humidity: (map['humidity'] as num?)?.toDouble(),
        updatedAt: _parseTimestamp(map['updatedAt']),
      );

  // Safe fridge range: 1–8°C
  bool get isAlert => temperatureC != null && (temperatureC! < 1 || temperatureC! > 8);
  bool get isOk => temperatureC != null && temperatureC! >= 1 && temperatureC! <= 8;

  String get updatedAtLabel {
    final now = DateTime.now();
    final diff = now.difference(updatedAt);
    if (diff.inMinutes < 1) return 'Just now';
    if (diff.inMinutes < 60) return '${diff.inMinutes}m ago';
    if (diff.inHours < 24) return '${diff.inHours}h ago';
    return '${diff.inDays}d ago';
  }
}

/// Door state from fridges/{id}/sensors/door
class DoorStatus {
  final String state; // "open" or "closed"
  final DateTime updatedAt;

  const DoorStatus({required this.state, required this.updatedAt});

  bool get isOpen => state == 'open';

  factory DoorStatus.fromMap(Map<String, dynamic> map) => DoorStatus(
        state: map['state'] as String? ?? 'unknown',
        updatedAt: _parseTimestamp(map['updatedAt']),
      );
}
