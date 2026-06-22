class FridgeItem {
  final String name;
  final String quantity;
  final String confidence;

  const FridgeItem({
    required this.name,
    required this.quantity,
    required this.confidence,
  });

  factory FridgeItem.fromMap(Map<String, dynamic> map) => FridgeItem(
        name: map['name'] as String? ?? '',
        quantity: map['quantity'] as String? ?? '',
        confidence: map['confidence'] as String? ?? 'low',
      );

  bool get isHighConfidence => confidence == 'high';
  bool get isMediumConfidence => confidence == 'medium';

  String get displayName =>
      name.isNotEmpty ? name[0].toUpperCase() + name.substring(1) : '';
}

/// Current inventory state from fridges/{id}/inventory/current
class InventorySnapshot {
  final List<FridgeItem> items;
  final String updatedAt;
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
      updatedAt: map['updatedAt'] as String? ?? '',
      source: map['source'] as String? ?? '',
    );
  }
}

/// Temperature/humidity from fridges/{id}/sensors/temperature
class TemperatureReading {
  final double? temperatureC;
  final double? humidity;
  final String updatedAt;

  const TemperatureReading({
    this.temperatureC,
    this.humidity,
    required this.updatedAt,
  });

  factory TemperatureReading.fromMap(Map<String, dynamic> map) =>
      TemperatureReading(
        temperatureC: (map['temperature'] as num?)?.toDouble(),
        humidity: (map['humidity'] as num?)?.toDouble(),
        updatedAt: map['updatedAt'] as String? ?? '',
      );

  // Safe fridge range: 1–8°C
  bool get isAlert => temperatureC != null && (temperatureC! < 1 || temperatureC! > 8);
  bool get isOk => temperatureC != null && temperatureC! >= 1 && temperatureC! <= 8;
}
