import 'package:cloud_firestore/cloud_firestore.dart';
import '../services/fridge_settings_service.dart';

// Parses either a Firestore Timestamp or an ESP32 string ("YYYY-MM-DD HH:MM:SS").
DateTime _parseTimestamp(dynamic v) {
  if (v == null) return DateTime.now();
  if (v is Timestamp) return v.toDate();
  if (v is String) {
    return DateTime.tryParse(v.replaceFirst(' ', 'T')) ?? DateTime.now();
  }
  return DateTime.now();
}

/// "Just now" / "5m ago" / "3h ago" / "2d ago" for any past timestamp.
String relativeTimeLabel(DateTime t) {
  final diff = DateTime.now().difference(t);
  if (diff.inMinutes < 1) return 'Just now';
  if (diff.inMinutes < 60) return '${diff.inMinutes}m ago';
  if (diff.inHours < 24) return '${diff.inHours}h ago';
  return '${diff.inDays}d ago';
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
      confidence: _normalizeConfidence(map['confidence']),
      expiries: rawExpiries.whereType<String>().toList(),
    );
  }

  // The AI scan writes "high"/"medium"/"low", but the GM65 barcode path
  // writes a numeric score (e.g. "100"). Map anything numeric onto the
  // same three-value domain the UI understands.
  static String _normalizeConfidence(dynamic v) {
    final s = (v as String?)?.toLowerCase().trim() ?? 'low';
    if (s == 'high' || s == 'medium' || s == 'low') return s;
    final n = int.tryParse(s);
    if (n != null) return n >= 70 ? 'high' : (n >= 40 ? 'medium' : 'low');
    return 'low';
  }

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

  // Whole days until the nearest expiry, comparing calendar dates so an
  // item expiring tomorrow is 1 (not 0) all day long. Null when no date.
  int? get _daysToExpiry {
    final next = nextExpiry;
    if (next == null) return null;
    final now = DateTime.now();
    final today = DateTime(now.year, now.month, now.day);
    return next.difference(today).inDays;
  }

  ExpiryStatus get expiryStatus {
    final days = _daysToExpiry;
    if (days == null) return ExpiryStatus.unknown;
    if (days < 0) return ExpiryStatus.expired;
    if (days <= FridgeSettingsService.cached.expiryWarnDays) {
      return ExpiryStatus.critical;
    }
    if (days <= 7) return ExpiryStatus.soon;
    return ExpiryStatus.ok;
  }

  // Human-readable label for the nearest expiry date.
  String get expiryLabel {
    final days = _daysToExpiry;
    if (days == null) return 'No date set';
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

  const InventorySnapshot({
    required this.items,
    required this.updatedAt,
  });

  factory InventorySnapshot.fromMap(Map<String, dynamic> map) {
    final raw = map['items'] as List<dynamic>? ?? [];
    return InventorySnapshot(
      items: raw.map((e) => FridgeItem.fromMap(e as Map<String, dynamic>)).toList(),
      updatedAt: _parseTimestamp(map['updatedAt']),
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

  // Safe range comes from Settings (defaults 2–8°C).
  bool get isAlert =>
      temperatureC != null &&
      (temperatureC! < FridgeSettingsService.cached.tempMin ||
          temperatureC! > FridgeSettingsService.cached.tempMax);
  bool get isOk => temperatureC != null && !isAlert;

  String get updatedAtLabel => relativeTimeLabel(updatedAt);
}

/// Door state from fridges/{id}/sensors/door
class DoorStatus {
  final String state; // "open" or "closed"
  final DateTime updatedAt;

  const DoorStatus({required this.state, required this.updatedAt});

  bool get isOpen => state == 'open';

  String get updatedAtLabel => relativeTimeLabel(updatedAt);

  factory DoorStatus.fromMap(Map<String, dynamic> map) => DoorStatus(
        state: map['state'] as String? ?? 'unknown',
        updatedAt: _parseTimestamp(map['updatedAt']),
      );
}
