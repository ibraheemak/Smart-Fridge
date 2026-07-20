import 'package:flutter/material.dart';
import '../models/fridge_item.dart';
import '../services/fridge_service.dart';
import '../theme/app_theme.dart';
import '../widgets/item_icon.dart';

class ExpiryScreen extends StatefulWidget {
  const ExpiryScreen({super.key});

  @override
  State<ExpiryScreen> createState() => _ExpiryScreenState();
}

class _ExpiryScreenState extends State<ExpiryScreen> {
  late final Stream<InventorySnapshot?> _invStream =
      FridgeService.inventoryStream();

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
        title: const Text('Expiry Alerts',
            style: TextStyle(
                color: AppColors.onSurface,
                fontSize: 20,
                fontWeight: FontWeight.w700)),
      ),
      body: StreamBuilder<InventorySnapshot?>(
        stream: _invStream,
        builder: (context, snap) {
          if (snap.connectionState == ConnectionState.waiting) {
            return const Center(
                child: CircularProgressIndicator(color: AppColors.primary));
          }
          final inv = snap.data;
          if (inv == null || inv.items.isEmpty) {
            return _emptyState(context);
          }

          final expired = inv.items
              .where((i) => i.expiryStatus == ExpiryStatus.expired)
              .toList();
          final critical = inv.items
              .where((i) => i.expiryStatus == ExpiryStatus.critical)
              .toList();
          final soon = inv.items
              .where((i) => i.expiryStatus == ExpiryStatus.soon)
              .toList();
          final ok = inv.items
              .where((i) => i.expiryStatus == ExpiryStatus.ok)
              .toList();
          final unknown = inv.items
              .where((i) => i.expiryStatus == ExpiryStatus.unknown)
              .toList();

          final hasAnything = expired.isNotEmpty ||
              critical.isNotEmpty ||
              soon.isNotEmpty ||
              ok.isNotEmpty;

          if (!hasAnything && unknown.length == inv.items.length) {
            return _noDateState(context, unknown.length);
          }

          return CustomScrollView(
            slivers: [
              // Expired
              if (expired.isNotEmpty) ...[
                _sectionHeader(
                    context,
                    'Expired',
                    '${expired.length} items',
                    AppColors.error,
                    Icons.cancel_rounded),
                SliverPadding(
                  padding: const EdgeInsets.symmetric(horizontal: 16),
                  sliver: SliverList(
                    delegate: SliverChildBuilderDelegate(
                      (_, i) => _ExpiryCard(item: expired[i]),
                      childCount: expired.length,
                    ),
                  ),
                ),
              ],

              // Critical (1–3 days)
              if (critical.isNotEmpty) ...[
                _sectionHeader(
                    context,
                    'Expiring in 1–3 days',
                    '${critical.length} items',
                    AppColors.error,
                    Icons.warning_rounded),
                SliverPadding(
                  padding: const EdgeInsets.symmetric(horizontal: 16),
                  sliver: SliverList(
                    delegate: SliverChildBuilderDelegate(
                      (_, i) => _ExpiryCard(item: critical[i]),
                      childCount: critical.length,
                    ),
                  ),
                ),
              ],

              // Soon (4–7 days)
              if (soon.isNotEmpty) ...[
                _sectionHeader(
                    context,
                    'Expiring this week',
                    '${soon.length} items',
                    AppColors.tertiaryFixedDim,
                    Icons.schedule_rounded),
                SliverPadding(
                  padding: const EdgeInsets.symmetric(horizontal: 16),
                  sliver: SliverList(
                    delegate: SliverChildBuilderDelegate(
                      (_, i) => _ExpiryCard(item: soon[i]),
                      childCount: soon.length,
                    ),
                  ),
                ),
              ],

              // Fresh (ok)
              if (ok.isNotEmpty) ...[
                _sectionHeader(
                    context,
                    'Fresh',
                    '${ok.length} items',
                    AppColors.secondary,
                    Icons.check_circle_rounded),
                SliverToBoxAdapter(
                  child: SizedBox(
                    height: 160,
                    child: ListView.separated(
                      scrollDirection: Axis.horizontal,
                      padding: const EdgeInsets.fromLTRB(16, 0, 16, 16),
                      separatorBuilder: (_, __) => const SizedBox(width: 12),
                      itemCount: ok.length,
                      itemBuilder: (_, i) => _FreshCard(item: ok[i]),
                    ),
                  ),
                ),
              ],

              // Unknown (no date set)
              if (unknown.isNotEmpty) ...[
                _sectionHeader(
                    context,
                    'No date set',
                    '${unknown.length} items',
                    AppColors.outline,
                    Icons.help_outline_rounded),
                SliverPadding(
                  padding: const EdgeInsets.symmetric(horizontal: 16),
                  sliver: SliverList(
                    delegate: SliverChildBuilderDelegate(
                      (_, i) => _ExpiryCard(item: unknown[i]),
                      childCount: unknown.length,
                    ),
                  ),
                ),
              ],

              const SliverToBoxAdapter(child: SizedBox(height: 32)),
            ],
          );
        },
      ),
    );
  }

  Widget _emptyState(BuildContext context) {
    return Center(
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          Container(
            width: 72,
            height: 72,
            decoration: const BoxDecoration(
              color: AppColors.secondaryContainer,
              shape: BoxShape.circle,
            ),
            child: const Icon(Icons.check_circle_rounded,
                size: 40, color: AppColors.onSecondaryContainer),
          ),
          const SizedBox(height: 16),
          Text('All fresh!', style: Theme.of(context).textTheme.titleLarge),
          const SizedBox(height: 8),
          const Text('No items in fridge.',
              style: TextStyle(color: AppColors.onSurfaceVariant)),
        ],
      ),
    );
  }

  Widget _noDateState(BuildContext context, int count) {
    return Center(
      child: Padding(
        padding: const EdgeInsets.all(32),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            const Icon(Icons.calendar_today_rounded,
                size: 64, color: AppColors.outline),
            const SizedBox(height: 16),
            Text('No expiry dates set',
                style: Theme.of(context).textTheme.titleLarge),
            const SizedBox(height: 8),
            Text(
              'Set expiry dates on the TFT display (touch UI) for $count items to see alerts here.',
              textAlign: TextAlign.center,
              style: const TextStyle(color: AppColors.onSurfaceVariant),
            ),
          ],
        ),
      ),
    );
  }

  SliverToBoxAdapter _sectionHeader(BuildContext context, String label,
      String count, Color color, IconData icon) {
    return SliverToBoxAdapter(
      child: Padding(
        padding: const EdgeInsets.fromLTRB(16, 20, 16, 10),
        child: Row(
          children: [
            Icon(icon, size: 18, color: color),
            const SizedBox(width: 8),
            Text(label,
                style: const TextStyle(
                    fontSize: 16,
                    fontWeight: FontWeight.w700,
                    color: AppColors.onSurface)),
            const Spacer(),
            Container(
              padding:
                  const EdgeInsets.symmetric(horizontal: 10, vertical: 3),
              decoration: BoxDecoration(
                color: color.withValues(alpha: 0.12),
                borderRadius: BorderRadius.circular(20),
              ),
              child: Text(count,
                  style: TextStyle(
                      fontSize: 11,
                      fontWeight: FontWeight.w700,
                      color: color)),
            ),
          ],
        ),
      ),
    );
  }
}

class _ExpiryCard extends StatelessWidget {
  final FridgeItem item;

  const _ExpiryCard({required this.item});

  Color get _borderColor {
    switch (item.expiryStatus) {
      case ExpiryStatus.expired:
      case ExpiryStatus.critical:
        return AppColors.error;
      case ExpiryStatus.soon:
        return AppColors.tertiaryFixedDim;
      case ExpiryStatus.ok:
        return AppColors.secondary;
      case ExpiryStatus.unknown:
        return AppColors.outline;
    }
  }

  Color get _badgeColor {
    switch (item.expiryStatus) {
      case ExpiryStatus.expired:
      case ExpiryStatus.critical:
        return AppColors.errorContainer;
      case ExpiryStatus.soon:
        return AppColors.tertiaryFixed;
      case ExpiryStatus.ok:
        return AppColors.secondaryContainer;
      case ExpiryStatus.unknown:
        return AppColors.surfaceContainerHigh;
    }
  }

  Color get _badgeTextColor {
    switch (item.expiryStatus) {
      case ExpiryStatus.expired:
      case ExpiryStatus.critical:
        return AppColors.error;
      case ExpiryStatus.soon:
        return const Color(0xFF5B4300);
      case ExpiryStatus.ok:
        return AppColors.secondary;
      case ExpiryStatus.unknown:
        return AppColors.onSurfaceVariant;
    }
  }

  @override
  Widget build(BuildContext context) {
    return Container(
      margin: const EdgeInsets.only(bottom: 10),
      decoration: BoxDecoration(
        color: AppColors.surfaceContainerLowest,
        borderRadius: BorderRadius.circular(14),
        border: Border(left: BorderSide(color: _borderColor, width: 4)),
        boxShadow: const [
          BoxShadow(
              color: Color(0x08000000), blurRadius: 6, offset: Offset(0, 2)),
        ],
      ),
      child: Padding(
        padding: const EdgeInsets.all(14),
        child: Row(
          children: [
            ItemIcon(itemName: item.name, size: 52),
            const SizedBox(width: 14),
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(item.displayName,
                      style: const TextStyle(
                          fontSize: 15, fontWeight: FontWeight.w700)),
                  const SizedBox(height: 2),
                  Text(item.quantity,
                      style: const TextStyle(
                          fontSize: 12, color: AppColors.onSurfaceVariant)),
                  const SizedBox(height: 6),
                  Container(
                    padding: const EdgeInsets.symmetric(
                        horizontal: 8, vertical: 3),
                    decoration: BoxDecoration(
                      color: _badgeColor,
                      borderRadius: BorderRadius.circular(20),
                    ),
                    child: Text(item.expiryLabel,
                        style: TextStyle(
                            fontSize: 11,
                            fontWeight: FontWeight.w700,
                            color: _badgeTextColor)),
                  ),
                ],
              ),
            ),
            IconButton(
              onPressed: () async {
                await FridgeService.addShoppingItem(item.name);
                if (context.mounted) {
                  ScaffoldMessenger.of(context).showSnackBar(SnackBar(
                    content: Text(
                        '${item.displayName} added to shopping list'),
                    backgroundColor: AppColors.primary,
                  ));
                }
              },
              icon: const Icon(Icons.add_shopping_cart_rounded,
                  color: AppColors.primary),
              tooltip: 'Add to shopping list',
            ),
          ],
        ),
      ),
    );
  }
}

class _FreshCard extends StatelessWidget {
  final FridgeItem item;

  const _FreshCard({required this.item});

  @override
  Widget build(BuildContext context) {
    return Container(
      width: 130,
      decoration: BoxDecoration(
        color: AppColors.surfaceContainerLowest,
        borderRadius: BorderRadius.circular(16),
        border:
            Border.all(color: AppColors.secondary.withValues(alpha: 0.3)),
        boxShadow: const [
          BoxShadow(
              color: Color(0x08000000), blurRadius: 6, offset: Offset(0, 2)),
        ],
      ),
      child: Padding(
        padding: const EdgeInsets.all(12),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            ItemIcon(itemName: item.name, size: 48),
            const Spacer(),
            Text(item.displayName,
                style: const TextStyle(
                    fontSize: 13, fontWeight: FontWeight.w700),
                maxLines: 2,
                overflow: TextOverflow.ellipsis),
            const SizedBox(height: 4),
            Container(
              padding:
                  const EdgeInsets.symmetric(horizontal: 6, vertical: 2),
              decoration: BoxDecoration(
                color: AppColors.secondaryContainer,
                borderRadius: BorderRadius.circular(20),
              ),
              child: Text(item.expiryLabel,
                  style: const TextStyle(
                      fontSize: 10,
                      fontWeight: FontWeight.w700,
                      color: AppColors.secondary)),
            ),
          ],
        ),
      ),
    );
  }
}
