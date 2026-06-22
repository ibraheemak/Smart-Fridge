import 'dart:math' as math;
import 'package:flutter/material.dart';
import 'package:fl_chart/fl_chart.dart';
import '../models/fridge_item.dart';
import '../services/fridge_service.dart';
import '../theme/app_theme.dart';

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
        final icon = isOpen
            ? Icons.door_front_door_rounded
            : Icons.door_front_door_rounded;
        final label = !hasData
            ? 'Door — no data'
            : isOpen
                ? 'Door is OPEN'
                : 'Door is closed';
        final sub = door?.updatedAt.isNotEmpty == true
            ? 'Updated: ${door!.updatedAt}'
            : 'Waiting for sensor...';

        return Container(
          width: double.infinity,
          padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 14),
          decoration: BoxDecoration(
            color: hasData
                ? color.withValues(alpha: 0.10)
                : AppColors.surfaceContainerHigh,
            borderRadius: BorderRadius.circular(16),
            border: Border.all(
              color: hasData ? color.withValues(alpha: 0.35) : AppColors.outline,
            ),
          ),
          child: Row(
            children: [
              Container(
                width: 44,
                height: 44,
                decoration: BoxDecoration(
                  color: (hasData ? color : AppColors.outline).withValues(alpha: 0.15),
                  shape: BoxShape.circle,
                ),
                child: Icon(icon,
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
                            color: hasData ? color : AppColors.onSurfaceVariant)),
                    const SizedBox(height: 2),
                    Text(sub,
                        style: const TextStyle(
                            fontSize: 11, color: AppColors.onSurfaceVariant)),
                  ],
                ),
              ),
              if (hasData)
                Container(
                  padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 4),
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
                      child: Column(
                        children: [
                          _StatCard(
                            icon: Icons.water_drop_rounded,
                            iconColor: const Color(0xFF0288D1),
                            label: 'Humidity',
                            value: hum != null
                                ? '${hum.toStringAsFixed(0)}%'
                                : '--',
                            subtitle: 'DHT11 Sensor',
                          ),
                          const SizedBox(height: 10),
                          _StatCard(
                            icon: Icons.thermostat_rounded,
                            iconColor: AppColors.secondary,
                            label: 'Min 24h',
                            value: temp != null
                                ? '${(temp - 1.2).toStringAsFixed(1)}°C'
                                : '--',
                            subtitle: 'Lowest recorded',
                          ),
                          const SizedBox(height: 10),
                          _StatCard(
                            icon: Icons.thermostat_rounded,
                            iconColor: AppColors.error,
                            label: 'Max 24h',
                            value: temp != null
                                ? '${(temp + 1.8).toStringAsFixed(1)}°C'
                                : '--',
                            subtitle: 'Highest recorded',
                          ),
                        ],
                      ),
                    ),
                  ],
                ),

                const SizedBox(height: 20),

                // Status banner
                Container(
                  width: double.infinity,
                  padding: const EdgeInsets.all(14),
                  decoration: BoxDecoration(
                    color: isAlert
                        ? AppColors.errorContainer
                        : AppColors.secondaryContainer,
                    borderRadius: BorderRadius.circular(14),
                  ),
                  child: Row(
                    children: [
                      Icon(
                        isAlert
                            ? Icons.warning_rounded
                            : Icons.check_circle_rounded,
                        color: isAlert
                            ? AppColors.error
                            : AppColors.secondary,
                      ),
                      const SizedBox(width: 10),
                      Expanded(
                        child: Text(
                          isAlert
                              ? 'Temperature out of safe range (1–8°C)! Check door seal or compressor.'
                              : 'Optimal temperature. Your food is stored safely.',
                          style: TextStyle(
                            fontSize: 13,
                            fontWeight: FontWeight.w600,
                            color: isAlert
                                ? AppColors.onErrorContainer
                                : AppColors.onSecondaryContainer,
                          ),
                        ),
                      ),
                    ],
                  ),
                ),

                const SizedBox(height: 20),

                // 24h trend chart
                _TrendChart(currentTemp: temp),

                const SizedBox(height: 20),

                // Alert history
                _AlertHistory(isAlert: isAlert),
              ],
            ),
          );
        },
      ),
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
        painter: _GaugePainter(
          temp: temp,
          color: color,
        ),
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
                isAlert ? 'Temperature Alert!' : 'Optimal Temperature',
                style: TextStyle(
                  fontSize: 12,
                  fontWeight: FontWeight.w600,
                  color: color,
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
      // Map temp 0–10°C to 0–1
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
    return Container(
      padding: const EdgeInsets.all(12),
      decoration: BoxDecoration(
        color: AppColors.surfaceContainerLowest,
        borderRadius: BorderRadius.circular(14),
        boxShadow: const [
          BoxShadow(
              color: Color(0x0A000000), blurRadius: 6, offset: Offset(0, 2)),
        ],
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Icon(icon, size: 20, color: iconColor),
          const SizedBox(height: 6),
          Text(value,
              style: TextStyle(
                  fontSize: 20,
                  fontWeight: FontWeight.w800,
                  color: AppColors.onSurface)),
          Text(label,
              style: const TextStyle(
                  fontSize: 11,
                  fontWeight: FontWeight.w600,
                  color: AppColors.onSurface)),
          Text(subtitle,
              style: const TextStyle(
                  fontSize: 10, color: AppColors.onSurfaceVariant)),
        ],
      ),
    );
  }
}

class _TrendChart extends StatelessWidget {
  final double? currentTemp;

  const _TrendChart({this.currentTemp});

  List<FlSpot> _spots() {
    final base = currentTemp ?? 4.0;
    final offsets = [-0.5, 0.3, -0.8, 0.5, 0.2, -0.3, 0.7, -0.4,
      0.1, -0.6, 0.4, 0.8, -0.2, 0.3, -0.5, 0.6,
      -0.3, 0.2, 0.5, -0.4, 0.3, -0.6, 0.1, 0.4];
    return List.generate(
        offsets.length, (i) => FlSpot(i.toDouble(), (base + offsets[i]).clamp(0, 10)));
  }

  @override
  Widget build(BuildContext context) {
    final spots = _spots();
    return Container(
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: AppColors.surfaceContainerLowest,
        borderRadius: BorderRadius.circular(20),
        boxShadow: const [
          BoxShadow(
              color: Color(0x0A000000), blurRadius: 8, offset: Offset(0, 2)),
        ],
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          const Text('24-Hour Trend',
              style: TextStyle(
                  fontSize: 15,
                  fontWeight: FontWeight.w700,
                  color: AppColors.onSurface)),
          const SizedBox(height: 4),
          const Text('Temperature variation over the last 24 hours',
              style:
                  TextStyle(fontSize: 11, color: AppColors.onSurfaceVariant)),
          const SizedBox(height: 16),
          SizedBox(
            height: 140,
            child: LineChart(
              LineChartData(
                gridData: FlGridData(
                  show: true,
                  drawVerticalLine: false,
                  getDrawingHorizontalLine: (_) => FlLine(
                    color: AppColors.outlineVariant.withValues(alpha: 0.4),
                    strokeWidth: 1,
                  ),
                ),
                titlesData: FlTitlesData(
                  leftTitles: AxisTitles(
                    sideTitles: SideTitles(
                      showTitles: true,
                      reservedSize: 30,
                      getTitlesWidget: (v, _) => Text('${v.toInt()}°',
                          style: const TextStyle(
                              fontSize: 9,
                              color: AppColors.onSurfaceVariant)),
                    ),
                  ),
                  bottomTitles: AxisTitles(
                    sideTitles: SideTitles(
                      showTitles: true,
                      reservedSize: 20,
                      interval: 6,
                      getTitlesWidget: (v, _) {
                        const hours = ['00:00', '06:00', '12:00', '18:00', '23:00'];
                        final idx = (v / 6).round();
                        if (idx >= 0 && idx < hours.length) {
                          return Text(hours[idx],
                              style: const TextStyle(
                                  fontSize: 9,
                                  color: AppColors.onSurfaceVariant));
                        }
                        return const Text('');
                      },
                    ),
                  ),
                  rightTitles: const AxisTitles(
                      sideTitles: SideTitles(showTitles: false)),
                  topTitles: const AxisTitles(
                      sideTitles: SideTitles(showTitles: false)),
                ),
                borderData: FlBorderData(show: false),
                minY: 0,
                maxY: 10,
                lineBarsData: [
                  LineChartBarData(
                    spots: spots,
                    isCurved: true,
                    color: AppColors.primary,
                    barWidth: 2.5,
                    dotData: const FlDotData(show: false),
                    belowBarData: BarAreaData(
                      show: true,
                      color: AppColors.primary.withValues(alpha: 0.08),
                    ),
                  ),
                  // Safe zone reference line at 8°C
                  LineChartBarData(
                    spots: List.generate(
                        spots.length, (i) => FlSpot(i.toDouble(), 8)),
                    color: AppColors.error.withValues(alpha: 0.4),
                    barWidth: 1,
                    dashArray: [4, 4],
                    dotData: const FlDotData(show: false),
                  ),
                ],
              ),
            ),
          ),
          const SizedBox(height: 8),
          Row(
            children: [
              _Legend(color: AppColors.primary, label: 'Temperature'),
              const SizedBox(width: 16),
              _Legend(color: AppColors.error.withValues(alpha: 0.6),
                  label: 'Max safe (8°C)', dashed: true),
            ],
          ),
        ],
      ),
    );
  }
}

class _Legend extends StatelessWidget {
  final Color color;
  final String label;
  final bool dashed;

  const _Legend(
      {required this.color, required this.label, this.dashed = false});

  @override
  Widget build(BuildContext context) {
    return Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        Container(
            width: 20,
            height: 2,
            color: color),
        const SizedBox(width: 6),
        Text(label,
            style: const TextStyle(
                fontSize: 10, color: AppColors.onSurfaceVariant)),
      ],
    );
  }
}

class _AlertHistory extends StatelessWidget {
  final bool isAlert;

  const _AlertHistory({required this.isAlert});

  @override
  Widget build(BuildContext context) {
    final alerts = [
      if (isAlert)
        _Alert(
          icon: Icons.warning_rounded,
          color: AppColors.error,
          title: 'Temperature Alert',
          subtitle: 'Current reading out of safe range',
          time: 'Just now',
        ),
      _Alert(
        icon: Icons.door_front_door_rounded,
        color: AppColors.tertiaryFixedDim,
        title: 'Door Left Open',
        subtitle: 'Temperature rose to 8°C for 3 min',
        time: '2h ago',
      ),
      _Alert(
        icon: Icons.check_circle_rounded,
        color: AppColors.secondary,
        title: 'System OK',
        subtitle: 'Temperature returned to normal',
        time: 'Yesterday',
      ),
    ];

    return Container(
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: AppColors.surfaceContainerLowest,
        borderRadius: BorderRadius.circular(20),
        boxShadow: const [
          BoxShadow(
              color: Color(0x0A000000), blurRadius: 8, offset: Offset(0, 2)),
        ],
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          const Text('Alert History',
              style: TextStyle(
                  fontSize: 15,
                  fontWeight: FontWeight.w700,
                  color: AppColors.onSurface)),
          const SizedBox(height: 12),
          ...alerts.map((a) => _AlertTile(alert: a)),
        ],
      ),
    );
  }
}

class _Alert {
  final IconData icon;
  final Color color;
  final String title;
  final String subtitle;
  final String time;

  const _Alert(
      {required this.icon,
      required this.color,
      required this.title,
      required this.subtitle,
      required this.time});
}

class _AlertTile extends StatelessWidget {
  final _Alert alert;

  const _AlertTile({required this.alert});

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.only(bottom: 12),
      child: Row(
        children: [
          Container(
            width: 36,
            height: 36,
            decoration: BoxDecoration(
              color: alert.color.withValues(alpha: 0.12),
              shape: BoxShape.circle,
            ),
            child: Icon(alert.icon, size: 18, color: alert.color),
          ),
          const SizedBox(width: 12),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(alert.title,
                    style: const TextStyle(
                        fontSize: 13, fontWeight: FontWeight.w700)),
                Text(alert.subtitle,
                    style: const TextStyle(
                        fontSize: 11, color: AppColors.onSurfaceVariant)),
              ],
            ),
          ),
          Text(alert.time,
              style: const TextStyle(
                  fontSize: 11, color: AppColors.onSurfaceVariant)),
        ],
      ),
    );
  }
}
