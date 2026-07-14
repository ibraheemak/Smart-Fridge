import 'dart:math' as math;
import 'package:flutter/material.dart';
import '../models/fridge_item.dart';
import '../services/fridge_service.dart';
import '../theme/app_theme.dart';

class TemperatureScreen extends StatelessWidget {
  const TemperatureScreen({super.key});

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: AppColors.background,
      appBar: AppBar(
        backgroundColor: AppColors.background,
        elevation: 0,
        scrolledUnderElevation: 0,
        leading: IconButton(
          icon: const Icon(Icons.arrow_back_rounded, color: AppColors.onSurface),
          onPressed: () => Navigator.pop(context),
        ),
        title: const Text('Temperature',
            style: TextStyle(
                color: AppColors.onSurface,
                fontSize: 20,
                fontWeight: FontWeight.w700)),
      ),
      body: StreamBuilder<TemperatureReading?>(
        stream: FridgeService.temperatureStream(),
        builder: (context, snap) {
          final reading = snap.data;
          final temp = reading?.temperatureC;
          final hum = reading?.humidity;
          final isAlert = reading?.isAlert ?? false;

          return SingleChildScrollView(
            padding: const EdgeInsets.fromLTRB(16, 8, 16, 32),
            child: Column(
              children: [
                // Door status — live from hall sensor
                const _DoorStatusCard(),
                const SizedBox(height: 16),

                // Gauge + humidity row
                Row(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Expanded(
                      flex: 3,
                      child: _TempGauge(temp: temp, isAlert: isAlert),
                    ),
                    const SizedBox(width: 12),
                    Expanded(
                      flex: 2,
                      child: _StatCard(
                        icon: Icons.water_drop_rounded,
                        iconColor: const Color(0xFF0288D1),
                        label: 'Humidity',
                        value: hum != null
                            ? '${hum.toStringAsFixed(0)}%'
                            : '--',
                        subtitle: 'DHT11 Sensor',
                      ),
                    ),
                  ],
                ),

                const SizedBox(height: 16),

                // Last updated
                if (reading != null)
                  Container(
                    width: double.infinity,
                    padding: const EdgeInsets.symmetric(
                        horizontal: 16, vertical: 10),
                    decoration: BoxDecoration(
                      color: AppColors.surfaceContainerLow,
                      borderRadius: BorderRadius.circular(12),
                    ),
                    child: Row(
                      children: [
                        const Icon(Icons.access_time_rounded,
                            size: 14, color: AppColors.onSurfaceVariant),
                        const SizedBox(width: 8),
                        Text(
                          'Last updated: ${reading.updatedAtLabel}',
                          style: const TextStyle(
                              fontSize: 12,
                              color: AppColors.onSurfaceVariant),
                        ),
                      ],
                    ),
                  ),

                const SizedBox(height: 16),

                // Status banner
                Container(
                  width: double.infinity,
                  padding: const EdgeInsets.all(14),
                  decoration: BoxDecoration(
                    color: isAlert
                        ? AppColors.errorContainer
                        : reading == null
                            ? AppColors.surfaceContainerHigh
                            : AppColors.secondaryContainer,
                    borderRadius: BorderRadius.circular(14),
                  ),
                  child: Row(
                    children: [
                      Icon(
                        reading == null
                            ? Icons.sensors_off_rounded
                            : isAlert
                                ? Icons.warning_rounded
                                : Icons.check_circle_rounded,
                        color: reading == null
                            ? AppColors.onSurfaceVariant
                            : isAlert
                                ? AppColors.error
                                : AppColors.secondary,
                      ),
                      const SizedBox(width: 10),
                      Expanded(
                        child: Text(
                          reading == null
                              ? 'No sensor data yet. The DHT11 on the CAM board will push readings automatically.'
                              : isAlert
                                  ? 'Temperature out of safe range (1–8°C)! Check door seal or compressor.'
                                  : 'Optimal temperature. Your food is stored safely.',
                          style: TextStyle(
                            fontSize: 13,
                            fontWeight: FontWeight.w600,
                            color: reading == null
                                ? AppColors.onSurfaceVariant
                                : isAlert
                                    ? AppColors.onErrorContainer
                                    : AppColors.onSecondaryContainer,
                          ),
                        ),
                      ),
                    ],
                  ),
                ),
              ],
            ),
          );
        },
      ),
    );
  }
}

class _DoorStatusCard extends StatelessWidget {
  const _DoorStatusCard();

  @override
  Widget build(BuildContext context) {
    return StreamBuilder<DoorStatus?>(
      stream: FridgeService.doorStream(),
      builder: (context, snap) {
        final door = snap.data;
        final isOpen = door?.isOpen ?? false;
        final hasData = door != null;

        final color = isOpen ? AppColors.error : AppColors.secondary;
        final label = !hasData
            ? 'Door — no data'
            : isOpen
                ? 'Door is OPEN'
                : 'Door is closed';

        String sub = 'Waiting for sensor...';
        if (hasData) {
          final diff = DateTime.now().difference(door.updatedAt);
          final timeLabel = diff.inMinutes < 1
              ? 'Just now'
              : diff.inMinutes < 60
                  ? '${diff.inMinutes}m ago'
                  : '${diff.inHours}h ago';
          sub = 'Updated: $timeLabel';
        }

        return Container(
          width: double.infinity,
          padding:
              const EdgeInsets.symmetric(horizontal: 16, vertical: 14),
          decoration: BoxDecoration(
            color: hasData
                ? color.withValues(alpha: 0.10)
                : AppColors.surfaceContainerHigh,
            borderRadius: BorderRadius.circular(16),
            border: Border.all(
              color: hasData
                  ? color.withValues(alpha: 0.35)
                  : AppColors.outline,
            ),
          ),
          child: Row(
            children: [
              Container(
                width: 44,
                height: 44,
                decoration: BoxDecoration(
                  color: (hasData ? color : AppColors.outline)
                      .withValues(alpha: 0.15),
                  shape: BoxShape.circle,
                ),
                child: Icon(Icons.door_front_door_rounded,
                    size: 22,
                    color: hasData ? color : AppColors.outline),
              ),
              const SizedBox(width: 14),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(label,
                        style: TextStyle(
                            fontSize: 15,
                            fontWeight: FontWeight.w700,
                            color: hasData
                                ? color
                                : AppColors.onSurfaceVariant)),
                    const SizedBox(height: 2),
                    Text(sub,
                        style: const TextStyle(
                            fontSize: 11,
                            color: AppColors.onSurfaceVariant)),
                  ],
                ),
              ),
              if (hasData)
                Container(
                  padding: const EdgeInsets.symmetric(
                      horizontal: 10, vertical: 4),
                  decoration: BoxDecoration(
                    color: color.withValues(alpha: 0.15),
                    borderRadius: BorderRadius.circular(20),
                  ),
                  child: Text(
                    isOpen ? 'OPEN' : 'CLOSED',
                    style: TextStyle(
                        fontSize: 11,
                        fontWeight: FontWeight.w800,
                        color: color),
                  ),
                ),
            ],
          ),
        );
      },
    );
  }
}

class _TempGauge extends StatelessWidget {
  final double? temp;
  final bool isAlert;

  const _TempGauge({this.temp, required this.isAlert});

  @override
  Widget build(BuildContext context) {
    final color = isAlert ? AppColors.error : AppColors.secondary;
    final displayTemp = temp != null ? '${temp!.toStringAsFixed(1)}°C' : '--';

    return Container(
      height: 200,
      decoration: BoxDecoration(
        color: AppColors.surfaceContainerLowest,
        borderRadius: BorderRadius.circular(20),
        boxShadow: const [
          BoxShadow(
              color: Color(0x0A000000),
              blurRadius: 8,
              offset: Offset(0, 2)),
        ],
      ),
      child: CustomPaint(
        painter: _GaugePainter(temp: temp, color: color),
        child: Center(
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              const SizedBox(height: 40),
              Text(
                displayTemp,
                style: TextStyle(
                  fontSize: 36,
                  fontWeight: FontWeight.w800,
                  color: color,
                ),
              ),
              const SizedBox(height: 4),
              Text(
                temp == null
                    ? 'No data'
                    : isAlert
                        ? 'Temperature Alert!'
                        : 'Optimal Temperature',
                style: TextStyle(
                  fontSize: 12,
                  fontWeight: FontWeight.w600,
                  color: temp == null ? AppColors.outline : color,
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}

class _GaugePainter extends CustomPainter {
  final double? temp;
  final Color color;

  _GaugePainter({this.temp, required this.color});

  @override
  void paint(Canvas canvas, Size size) {
    final cx = size.width / 2;
    final cy = size.height * 0.55;
    final radius = size.width * 0.38;

    final bgPaint = Paint()
      ..color = AppColors.surfaceContainerHighest
      ..style = PaintingStyle.stroke
      ..strokeWidth = 12
      ..strokeCap = StrokeCap.round;

    final fgPaint = Paint()
      ..color = color
      ..style = PaintingStyle.stroke
      ..strokeWidth = 12
      ..strokeCap = StrokeCap.round;

    const startAngle = math.pi * 0.75;
    const sweepAngle = math.pi * 1.5;

    canvas.drawArc(
      Rect.fromCircle(center: Offset(cx, cy), radius: radius),
      startAngle,
      sweepAngle,
      false,
      bgPaint,
    );

    if (temp != null) {
      final t = ((temp! - 0) / 10).clamp(0.0, 1.0);
      canvas.drawArc(
        Rect.fromCircle(center: Offset(cx, cy), radius: radius),
        startAngle,
        sweepAngle * t,
        false,
        fgPaint,
      );
    }
  }

  @override
  bool shouldRepaint(_GaugePainter old) =>
      old.temp != temp || old.color != color;
}

class _StatCard extends StatelessWidget {
  final IconData icon;
  final Color iconColor;
  final String label;
  final String value;
  final String subtitle;

  const _StatCard({
    required this.icon,
    required this.iconColor,
    required this.label,
    required this.value,
    required this.subtitle,
  });

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      height: 200,
      child: Container(
        padding: const EdgeInsets.all(12),
        decoration: BoxDecoration(
          color: AppColors.surfaceContainerLowest,
          borderRadius: BorderRadius.circular(14),
          boxShadow: const [
            BoxShadow(
                color: Color(0x0A000000),
                blurRadius: 6,
                offset: Offset(0, 2)),
          ],
        ),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            Icon(icon, size: 28, color: iconColor),
            const SizedBox(height: 8),
            Text(value,
                style: TextStyle(
                    fontSize: 28,
                    fontWeight: FontWeight.w800,
                    color: AppColors.onSurface)),
            const SizedBox(height: 4),
            Text(label,
                style: const TextStyle(
                    fontSize: 13,
                    fontWeight: FontWeight.w600,
                    color: AppColors.onSurface)),
            const SizedBox(height: 2),
            Text(subtitle,
                style: const TextStyle(
                    fontSize: 11, color: AppColors.onSurfaceVariant)),
          ],
        ),
      ),
    );
  }
}
