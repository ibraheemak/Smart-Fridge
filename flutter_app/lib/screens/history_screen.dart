import 'package:fl_chart/fl_chart.dart';
import 'package:flutter/material.dart';
import '../models/scan_record.dart';
import '../services/fridge_service.dart';
import '../theme/app_theme.dart';
import '../widgets/confidence_badge.dart';

class HistoryScreen extends StatefulWidget {
  const HistoryScreen({super.key});

  @override
  State<HistoryScreen> createState() => _HistoryScreenState();
}

class _HistoryScreenState extends State<HistoryScreen> {
  int _expanded = -1;

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: SafeArea(
        child: StreamBuilder<List<ScanRecord>>(
          stream: FridgeService.scanHistoryStream(),
          builder: (context, snap) {
            final scans = snap.data ?? [];

            return CustomScrollView(
              slivers: [
                SliverToBoxAdapter(
                  child: Padding(
                    padding: const EdgeInsets.fromLTRB(20, 24, 20, 16),
                    child: Text(
                      'Scan History',
                      style: Theme.of(context).textTheme.headlineMedium,
                    ),
                  ),
                ),

                // Chart
                if (scans.length >= 2)
                  SliverToBoxAdapter(
                    child: Padding(
                      padding:
                          const EdgeInsets.fromLTRB(16, 0, 16, 16),
                      child: _ItemCountChart(scans: scans),
                    ),
                  ),

                // Empty state
                if (scans.isEmpty &&
                    snap.connectionState != ConnectionState.waiting)
                  SliverFillRemaining(
                    child: Center(
                      child: Column(
                        mainAxisSize: MainAxisSize.min,
                        children: [
                          const Text('📋',
                              style: TextStyle(fontSize: 72)),
                          const SizedBox(height: 16),
                          Text('No scans yet',
                              style: Theme.of(context)
                                  .textTheme
                                  .titleLarge),
                          const SizedBox(height: 8),
                          const Text(
                            'Close the fridge door to trigger the first scan',
                            style: TextStyle(
                                color: AppColors.textSecondary),
                          ),
                        ],
                      ),
                    ),
                  )
                else
                  SliverPadding(
                    padding:
                        const EdgeInsets.fromLTRB(16, 0, 16, 100),
                    sliver: SliverList(
                      delegate: SliverChildBuilderDelegate(
                        (_, i) => Padding(
                          padding: const EdgeInsets.only(bottom: 10),
                          child: _ScanTile(
                            scan: scans[i],
                            expanded: _expanded == i,
                            onTap: () => setState(() =>
                                _expanded = _expanded == i ? -1 : i),
                          ),
                        ),
                        childCount: scans.length,
                      ),
                    ),
                  ),
              ],
            );
          },
        ),
      ),
    );
  }
}

// ── Bar chart: items per scan ──────────────────────────────────────────────
class _ItemCountChart extends StatelessWidget {
  final List<ScanRecord> scans;
  const _ItemCountChart({required this.scans});

  @override
  Widget build(BuildContext context) {
    final data = scans.take(8).toList().reversed.toList();
    final maxY = data
            .map((s) => s.itemCount)
            .fold(0, (a, b) => a > b ? a : b)
            .toDouble() +
        2;

    return Container(
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: AppColors.card,
        borderRadius: BorderRadius.circular(16),
        boxShadow: const [
          BoxShadow(
              color: AppColors.shadow, blurRadius: 12, offset: Offset(0, 4))
        ],
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text('Items detected',
              style: Theme.of(context).textTheme.titleMedium),
          const Text('Last 8 scans',
              style:
                  TextStyle(fontSize: 12, color: AppColors.textSecondary)),
          const SizedBox(height: 16),
          SizedBox(
            height: 130,
            child: BarChart(
              BarChartData(
                alignment: BarChartAlignment.spaceAround,
                maxY: maxY,
                barTouchData: BarTouchData(
                  touchTooltipData: BarTouchTooltipData(
                    getTooltipColor: (_) =>
                        AppColors.primary.withValues(alpha: 0.9),
                    getTooltipItem: (group, _, rod, __) => BarTooltipItem(
                      '${rod.toY.toInt()} items',
                      const TextStyle(
                          color: Colors.white,
                          fontSize: 12,
                          fontWeight: FontWeight.w600),
                    ),
                  ),
                ),
                titlesData: FlTitlesData(
                  leftTitles: const AxisTitles(
                      sideTitles: SideTitles(showTitles: false)),
                  rightTitles: const AxisTitles(
                      sideTitles: SideTitles(showTitles: false)),
                  topTitles: const AxisTitles(
                      sideTitles: SideTitles(showTitles: false)),
                  bottomTitles: AxisTitles(
                    sideTitles: SideTitles(
                      showTitles: true,
                      getTitlesWidget: (v, _) {
                        final i = v.toInt();
                        if (i < 0 || i >= data.length) {
                          return const SizedBox();
                        }
                        final ts = data[i].timestamp;
                        // Show HH:MM
                        final parts = ts.split(' ');
                        final time = parts.length > 1
                            ? parts[1].substring(0, 5)
                            : ts.substring(0, 5);
                        return Padding(
                          padding: const EdgeInsets.only(top: 4),
                          child: Text(time,
                              style: const TextStyle(
                                  fontSize: 9,
                                  color: AppColors.textSecondary)),
                        );
                      },
                    ),
                  ),
                ),
                borderData: FlBorderData(show: false),
                gridData: FlGridData(
                  show: true,
                  drawVerticalLine: false,
                  horizontalInterval: 2,
                  getDrawingHorizontalLine: (_) => const FlLine(
                    color: AppColors.divider,
                    strokeWidth: 1,
                  ),
                ),
                barGroups: List.generate(
                  data.length,
                  (i) => BarChartGroupData(
                    x: i,
                    barRods: [
                      BarChartRodData(
                        toY: data[i].itemCount.toDouble(),
                        color: AppColors.primary,
                        width: 22,
                        borderRadius: const BorderRadius.vertical(
                            top: Radius.circular(6)),
                        backDrawRodData: BackgroundBarChartRodData(
                          show: true,
                          toY: maxY,
                          color: AppColors.primary.withValues(alpha: 0.06),
                        ),
                      ),
                    ],
                  ),
                ),
              ),
            ),
          ),
        ],
      ),
    );
  }
}

// ── Expandable scan card ───────────────────────────────────────────────────
class _ScanTile extends StatelessWidget {
  final ScanRecord scan;
  final bool expanded;
  final VoidCallback onTap;

  const _ScanTile({
    required this.scan,
    required this.expanded,
    required this.onTap,
  });

  @override
  Widget build(BuildContext context) {
    return GestureDetector(
      onTap: onTap,
      child: AnimatedContainer(
        duration: const Duration(milliseconds: 200),
        decoration: BoxDecoration(
          color: AppColors.card,
          borderRadius: BorderRadius.circular(14),
          boxShadow: const [
            BoxShadow(
                color: AppColors.shadow,
                blurRadius: 8,
                offset: Offset(0, 2))
          ],
        ),
        child: Column(
          children: [
            // Row header
            Padding(
              padding: const EdgeInsets.all(14),
              child: Row(
                children: [
                  Container(
                    padding: const EdgeInsets.all(8),
                    decoration: BoxDecoration(
                      color: AppColors.primary.withValues(alpha: 0.1),
                      borderRadius: BorderRadius.circular(10),
                    ),
                    child: const Icon(Icons.camera_alt_rounded,
                        size: 18, color: AppColors.primary),
                  ),
                  const SizedBox(width: 12),
                  Expanded(
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Text(
                          scan.timestamp,
                          style: const TextStyle(
                              fontSize: 13,
                              fontWeight: FontWeight.w600,
                              color: AppColors.text),
                        ),
                        Text(
                          '${scan.itemCount} item${scan.itemCount == 1 ? '' : 's'} detected',
                          style: const TextStyle(
                              fontSize: 12,
                              color: AppColors.textSecondary),
                        ),
                      ],
                    ),
                  ),
                  Icon(
                    expanded
                        ? Icons.expand_less_rounded
                        : Icons.expand_more_rounded,
                    color: AppColors.textSecondary,
                  ),
                ],
              ),
            ),

            // Expanded item chips
            if (expanded) ...[
              const Divider(color: AppColors.divider, height: 1),
              Padding(
                padding: const EdgeInsets.all(12),
                child: Wrap(
                  spacing: 6,
                  runSpacing: 6,
                  children: scan.items.map((item) {
                    return Container(
                      padding: const EdgeInsets.symmetric(
                          horizontal: 10, vertical: 5),
                      decoration: BoxDecoration(
                        color: AppColors.background,
                        borderRadius: BorderRadius.circular(20),
                        border:
                            Border.all(color: AppColors.divider),
                      ),
                      child: Row(
                        mainAxisSize: MainAxisSize.min,
                        children: [
                          ConfidenceBadge(
                              confidence: item.confidence,
                              compact: true),
                          const SizedBox(width: 6),
                          Text(
                            '${item.displayName}  ·  ${item.quantity}',
                            style: const TextStyle(
                                fontSize: 12,
                                color: AppColors.text),
                          ),
                        ],
                      ),
                    );
                  }).toList(),
                ),
              ),
            ],
          ],
        ),
      ),
    );
  }
}
