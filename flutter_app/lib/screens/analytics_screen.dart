import 'package:flutter/material.dart';
import 'package:fl_chart/fl_chart.dart';
import '../models/fridge_item.dart';
import '../models/scan_record.dart';
import '../services/fridge_service.dart';
import '../theme/app_theme.dart';
import 'history_screen.dart';

class AnalyticsScreen extends StatefulWidget {
  const AnalyticsScreen({super.key});

  @override
  State<AnalyticsScreen> createState() => _AnalyticsScreenState();
}

class _AnalyticsScreenState extends State<AnalyticsScreen> {
  late final Stream<List<ScanRecord>> _scanStream =
      FridgeService.scanHistoryStream();
  late final Stream<InventorySnapshot?> _invStream =
      FridgeService.inventoryStream();
  late final Stream<Map<String, int>> _boughtStream =
      FridgeService.boughtCountsStream();

  // Server-side counts — never capped by the 20-scan history window.
  late final Future<int?> _totalScans = FridgeService.totalScanCount();
  late final Future<int?> _weekCount =
      FridgeService.scanCountSince(_weekStartIso);
  late final Future<List<ScanRecord>> _weekScans =
      FridgeService.scansSince(_weekStartIso);

  // Monday 00:00 of the current week.
  static DateTime get _weekStart {
    final now = DateTime.now();
    final today = DateTime(now.year, now.month, now.day);
    return today.subtract(Duration(days: today.weekday - 1));
  }

  static String get _weekStartIso {
    final s = _weekStart;
    return '${s.year}-${s.month.toString().padLeft(2, '0')}-'
        '${s.day.toString().padLeft(2, '0')} 00:00:00';
  }

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
        title: const Text('Analytics',
            style: TextStyle(
                color: AppColors.onSurface,
                fontSize: 20,
                fontWeight: FontWeight.w700)),
      ),
      body: StreamBuilder<List<ScanRecord>>(
        stream: _scanStream,
        builder: (context, snap) {
          final scans = snap.data ?? [];

          return SingleChildScrollView(
            padding: const EdgeInsets.fromLTRB(16, 8, 16, 32),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                // Summary row
                Row(
                  children: [
                    // True total from a server-side count — not capped at
                    // the 20-scan history window.
                    FutureBuilder<int?>(
                      future: _totalScans,
                      builder: (_, totalSnap) => _SummaryCard(
                        label: 'Camera Scans',
                        value: totalSnap.data?.toString() ?? '—',
                        icon: Icons.document_scanner_rounded,
                        color: AppColors.primary,
                      ),
                    ),
                    const SizedBox(width: 12),
                    StreamBuilder<InventorySnapshot?>(
                      stream: _invStream,
                      builder: (_, invSnap) => _SummaryCard(
                        label: 'Items in Fridge',
                        value: '${invSnap.data?.items.length ?? 0}',
                        icon: Icons.inventory_2_rounded,
                        color: AppColors.secondary,
                      ),
                    ),
                    const SizedBox(width: 12),
                    FutureBuilder<int?>(
                      future: _weekCount,
                      builder: (_, weekSnap) => _SummaryCard(
                        label: 'Scans This Week',
                        value: weekSnap.data?.toString() ?? '—',
                        icon: Icons.calendar_today_rounded,
                        color: AppColors.tertiaryFixedDim,
                      ),
                    ),
                  ],
                ),

                const SizedBox(height: 20),

                // Weekly activity chart — from an uncapped week query
                FutureBuilder<List<ScanRecord>>(
                  future: _weekScans,
                  builder: (_, weekSnap) => _WeeklyChart(
                    scans: weekSnap.data ?? [],
                    weekStart: _weekStart,
                  ),
                ),

                const SizedBox(height: 20),

                // Top items — from the firmware's monthly purchase counters,
                // falling back to recent-scan frequency if none exist yet.
                StreamBuilder<Map<String, int>>(
                  stream: _boughtStream,
                  builder: (_, boughtSnap) => _TopItems(
                    boughtCounts: boughtSnap.data ?? {},
                    scans: scans,
                  ),
                ),

                const SizedBox(height: 20),

                // Recent scans + "Full History" link
                _RecentScansList(scans: scans.take(5).toList()),

                const SizedBox(height: 8),

                Center(
                  child: TextButton.icon(
                    onPressed: () => Navigator.push(
                        context,
                        MaterialPageRoute(
                            builder: (_) => const HistoryScreen())),
                    icon: const Icon(Icons.history_rounded,
                        color: AppColors.primary, size: 18),
                    label: const Text(
                      'Full Scan History →',
                      style: TextStyle(
                          color: AppColors.primary,
                          fontWeight: FontWeight.w600),
                    ),
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

class _SummaryCard extends StatelessWidget {
  final String label;
  final String value;
  final IconData icon;
  final Color color;

  const _SummaryCard(
      {required this.label,
      required this.value,
      required this.icon,
      required this.color});

  @override
  Widget build(BuildContext context) {
    return Expanded(
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
          children: [
            Icon(icon, size: 22, color: color),
            const SizedBox(height: 6),
            Text(value,
                style: const TextStyle(
                    fontSize: 22,
                    fontWeight: FontWeight.w800,
                    color: AppColors.onSurface)),
            Text(label,
                style: const TextStyle(
                    fontSize: 10, color: AppColors.onSurfaceVariant),
                textAlign: TextAlign.center),
          ],
        ),
      ),
    );
  }
}

class _WeeklyChart extends StatelessWidget {
  final List<ScanRecord> scans;
  final DateTime weekStart;

  const _WeeklyChart({required this.scans, required this.weekStart});

  List<double> _scansPerDay() {
    final counts = List<double>.filled(7, 0);
    for (final s in scans) {
      final d = s.parsedTimestamp;
      if (d == null || d.isBefore(weekStart)) continue;
      counts[d.weekday - 1]++;
    }
    return counts;
  }

  @override
  Widget build(BuildContext context) {
    final counts = _scansPerDay();
    const days = ['Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat', 'Sun'];

    return Container(
      padding: const EdgeInsets.all(16),
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
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          const Text('Weekly Activity',
              style: TextStyle(
                  fontSize: 15,
                  fontWeight: FontWeight.w700,
                  color: AppColors.onSurface)),
          const SizedBox(height: 4),
          const Text('Scans per day this week',
              style: TextStyle(
                  fontSize: 11, color: AppColors.onSurfaceVariant)),
          const SizedBox(height: 16),
          SizedBox(
            height: 160,
            child: BarChart(
              BarChartData(
                alignment: BarChartAlignment.spaceEvenly,
                maxY: (counts.reduce((a, b) => a > b ? a : b) + 2)
                    .clamp(4, double.infinity),
                barGroups: List.generate(
                  7,
                  (i) => BarChartGroupData(
                    x: i,
                    barRods: [
                      BarChartRodData(
                        toY: counts[i],
                        color: counts[i] > 0
                            ? AppColors.primary
                            : AppColors.surfaceContainerHighest,
                        width: 24,
                        borderRadius: BorderRadius.circular(6),
                      ),
                    ],
                  ),
                ),
                titlesData: FlTitlesData(
                  bottomTitles: AxisTitles(
                    sideTitles: SideTitles(
                      showTitles: true,
                      reservedSize: 24,
                      getTitlesWidget: (v, _) => Text(
                        days[v.toInt()],
                        style: const TextStyle(
                            fontSize: 10,
                            color: AppColors.onSurfaceVariant),
                      ),
                    ),
                  ),
                  leftTitles: AxisTitles(
                    sideTitles: SideTitles(
                      showTitles: true,
                      reservedSize: 24,
                      getTitlesWidget: (v, _) => Text(
                        '${v.toInt()}',
                        style: const TextStyle(
                            fontSize: 9,
                            color: AppColors.onSurfaceVariant),
                      ),
                    ),
                  ),
                  rightTitles: const AxisTitles(
                      sideTitles: SideTitles(showTitles: false)),
                  topTitles: const AxisTitles(
                      sideTitles: SideTitles(showTitles: false)),
                ),
                gridData: FlGridData(
                  drawVerticalLine: false,
                  getDrawingHorizontalLine: (_) => FlLine(
                    color: AppColors.outlineVariant.withValues(alpha: 0.4),
                    strokeWidth: 1,
                  ),
                ),
                borderData: FlBorderData(show: false),
              ),
            ),
          ),
        ],
      ),
    );
  }
}

class _TopItems extends StatelessWidget {
  /// item name → times bought this month (from fridges/{id}/bought/{YYYY-MM},
  /// maintained by the CAM firmware). Empty when no purchases recorded yet.
  final Map<String, int> boughtCounts;
  final List<ScanRecord> scans;

  const _TopItems({required this.boughtCounts, required this.scans});

  Map<String, int> _scanCounts() {
    final counts = <String, int>{};
    for (final s in scans) {
      for (final item in s.items) {
        counts[item.displayName] = (counts[item.displayName] ?? 0) + 1;
      }
    }
    return counts;
  }

  @override
  Widget build(BuildContext context) {
    final usingBought = boughtCounts.isNotEmpty;
    final counts = usingBought
        ? boughtCounts.map((k, v) => MapEntry(
            k.isNotEmpty ? k[0].toUpperCase() + k.substring(1) : k, v))
        : _scanCounts();
    if (counts.isEmpty) return const SizedBox.shrink();
    final sorted = counts.entries.toList()
      ..sort((a, b) => b.value.compareTo(a.value));
    final top = sorted.take(5).toList();
    final maxCount = top.first.value.toDouble();

    return Container(
      padding: const EdgeInsets.all(16),
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
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(usingBought ? 'Top Purchases This Month' : 'Top Detected Items',
              style: const TextStyle(
                  fontSize: 15,
                  fontWeight: FontWeight.w700,
                  color: AppColors.onSurface)),
          const SizedBox(height: 4),
          Text(
              usingBought
                  ? 'Times each item was restocked this month'
                  : 'Detected across the last ${scans.length} scans',
              style: const TextStyle(
                  fontSize: 11, color: AppColors.onSurfaceVariant)),
          const SizedBox(height: 16),
          ...top.asMap().entries.map((entry) {
            final i = entry.key;
            final e = entry.value;
            return Padding(
              padding: const EdgeInsets.only(bottom: 12),
              child: Row(
                children: [
                  Container(
                    width: 24,
                    height: 24,
                    decoration: const BoxDecoration(
                      color: AppColors.primaryFixed,
                      shape: BoxShape.circle,
                    ),
                    child: Center(
                      child: Text('${i + 1}',
                          style: const TextStyle(
                              fontSize: 11,
                              fontWeight: FontWeight.w800,
                              color: AppColors.primary)),
                    ),
                  ),
                  const SizedBox(width: 10),
                  Expanded(
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Row(
                          mainAxisAlignment: MainAxisAlignment.spaceBetween,
                          children: [
                            Text(e.key,
                                style: const TextStyle(
                                    fontSize: 13,
                                    fontWeight: FontWeight.w600)),
                            Text('${e.value}×',
                                style: const TextStyle(
                                    fontSize: 12,
                                    color: AppColors.onSurfaceVariant)),
                          ],
                        ),
                        const SizedBox(height: 4),
                        ClipRRect(
                          borderRadius: BorderRadius.circular(4),
                          child: LinearProgressIndicator(
                            value: e.value / maxCount,
                            backgroundColor: AppColors.surfaceContainerHighest,
                            valueColor: const AlwaysStoppedAnimation<Color>(
                                AppColors.primary),
                            minHeight: 6,
                          ),
                        ),
                      ],
                    ),
                  ),
                ],
              ),
            );
          }),
        ],
      ),
    );
  }
}

class _RecentScansList extends StatelessWidget {
  final List<ScanRecord> scans;

  const _RecentScansList({required this.scans});

  @override
  Widget build(BuildContext context) {
    if (scans.isEmpty) return const SizedBox.shrink();

    return Container(
      padding: const EdgeInsets.all(16),
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
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          const Text('Recent Scans',
              style: TextStyle(
                  fontSize: 15,
                  fontWeight: FontWeight.w700,
                  color: AppColors.onSurface)),
          const SizedBox(height: 12),
          ...scans.map((s) => Padding(
                padding: const EdgeInsets.only(bottom: 10),
                child: Row(
                  children: [
                    Container(
                      width: 36,
                      height: 36,
                      decoration: BoxDecoration(
                        color: AppColors.primaryFixed,
                        borderRadius: BorderRadius.circular(10),
                      ),
                      child: const Icon(Icons.document_scanner_rounded,
                          size: 18, color: AppColors.primary),
                    ),
                    const SizedBox(width: 12),
                    Expanded(
                      child: Column(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: [
                          Text('${s.itemCount} items detected',
                              style: const TextStyle(
                                  fontSize: 13,
                                  fontWeight: FontWeight.w600)),
                          Text(s.timestamp,
                              style: const TextStyle(
                                  fontSize: 11,
                                  color: AppColors.onSurfaceVariant)),
                        ],
                      ),
                    ),
                    Text(s.source,
                        style: const TextStyle(
                            fontSize: 10,
                            color: AppColors.onSurfaceVariant)),
                  ],
                ),
              )),
        ],
      ),
    );
  }
}
