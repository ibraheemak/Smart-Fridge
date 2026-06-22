import 'package:flutter/material.dart';
import '../models/fridge_item.dart';
import '../services/fridge_service.dart';
import '../theme/app_theme.dart';
import '../widgets/item_icon.dart';

class ExpiryScreen extends StatelessWidget {
  const ExpiryScreen({super.key});

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
        actions: [
          IconButton(
            icon: const Icon(Icons.notifications_outlined,
                color: AppColors.primary),
            onPressed: () {},
          ),
        ],
      ),
      body: StreamBuilder<InventorySnapshot?>(
        stream: FridgeService.inventoryStream(),
        builder: (context, snap) {
          if (snap.connectionState == ConnectionState.waiting) {
            return const Center(
                child: CircularProgressIndicator(color: AppColors.primary));
          }
          final inv = snap.data;
          if (inv == null || inv.items.isEmpty) {
            return _emptyState(context);
          }

          final expired =
              inv.items.where((i) => i.confidence == 'low').toList();
          final soon =
              inv.items.where((i) => i.confidence == 'medium').toList();
          final fresh =
              inv.items.where((i) => i.confidence == 'high').toList();

          return CustomScrollView(
            slivers: [
              SliverToBoxAdapter(
                child: Padding(
                  padding: const EdgeInsets.fromLTRB(16, 8, 16, 0),
                  child: Container(
                    padding: const EdgeInsets.all(12),
                    decoration: BoxDecoration(
                      color: AppColors.primaryFixed,
                      borderRadius: BorderRadius.circular(12),
                    ),
                    child: const Row(
                      children: [
                        Icon(Icons.info_outline_rounded,
                            size: 16, color: AppColors.primary),
                        SizedBox(width: 8),
                        Expanded(
                          child: Text(
                            'Freshness is estimated from AI scan confidence. '
                            'Red = Low confidence (check item), '
                            'Amber = Medium, Green = Fresh.',
                            style: TextStyle(
                                fontSize: 11, color: AppColors.primary),
                          ),
                        ),
                      ],
                    ),
                  ),
                ),
              ),

              // Check Now (low confidence) — shown as "expired"
              if (expired.isNotEmpty) ...[
                _sectionHeader(
                    context,
                    'Check Now',
                    '${expired.length} items',
                    AppColors.error,
                    Icons.warning_rounded),
                SliverPadding(
                  padding: const EdgeInsets.symmetric(horizontal: 16),
                  sliver: SliverList(
                    delegate: SliverChildBuilderDelegate(
                      (_, i) => _ExpiryCard(
                        item: expired[i],
                        status: _ItemStatus.check,
                        daysLabel: 'Check immediately',
                      ),
                      childCount: expired.length,
                    ),
                  ),
                ),
              ],

              // Expiring Soon (medium confidence)
              if (soon.isNotEmpty) ...[
                _sectionHeader(
                    context,
                    'Expiring Soon',
                    '${soon.length} items',
                    AppColors.tertiaryFixedDim,
                    Icons.schedule_rounded),
                SliverPadding(
                  padding: const EdgeInsets.symmetric(horizontal: 16),
                  sliver: SliverList(
                    delegate: SliverChildBuilderDelegate(
                      (_, i) => _ExpiryCard(
                        item: soon[i],
                        status: _ItemStatus.soon,
                        daysLabel: '2–5 days left',
                      ),
                      childCount: soon.length,
                    ),
                  ),
                ),
              ],

              // Fresh (high confidence) — horizontal scroll
              if (fresh.isNotEmpty) ...[
                _sectionHeader(
                    context,
                    'Coming Up Soon',
                    '${fresh.length} items',
                    AppColors.secondary,
                    Icons.check_circle_rounded),
                SliverToBoxAdapter(
                  child: SizedBox(
                    height: 160,
                    child: ListView.separated(
                      scrollDirection: Axis.horizontal,
                      padding: const EdgeInsets.fromLTRB(16, 0, 16, 16),
                      separatorBuilder: (_, __) => const SizedBox(width: 12),
                      itemCount: fresh.length,
                      itemBuilder: (_, i) => _FreshCard(item: fresh[i]),
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
          Text('All fresh!',
              style: Theme.of(context).textTheme.titleLarge),
          const SizedBox(height: 8),
          const Text('No expiry concerns detected.',
              style: TextStyle(color: AppColors.onSurfaceVariant)),
        ],
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
                style: TextStyle(
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

enum _ItemStatus { check, soon, fresh }

class _ExpiryCard extends StatelessWidget {
  final FridgeItem item;
  final _ItemStatus status;
  final String daysLabel;

  const _ExpiryCard(
      {required this.item,
      required this.status,
      required this.daysLabel});

  Color get _borderColor {
    switch (status) {
      case _ItemStatus.check:
        return AppColors.error;
      case _ItemStatus.soon:
        return AppColors.tertiaryFixedDim;
      case _ItemStatus.fresh:
        return AppColors.secondary;
    }
  }

  Color get _badgeColor {
    switch (status) {
      case _ItemStatus.check:
        return AppColors.errorContainer;
      case _ItemStatus.soon:
        return AppColors.tertiaryFixed;
      case _ItemStatus.fresh:
        return AppColors.secondaryContainer;
    }
  }

  Color get _badgeTextColor {
    switch (status) {
      case _ItemStatus.check:
        return AppColors.error;
      case _ItemStatus.soon:
        return Color(0xFF5B4300);
      case _ItemStatus.fresh:
        return AppColors.secondary;
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
              color: Color(0x08000000),
              blurRadius: 6,
              offset: Offset(0, 2)),
        ],
      ),
      child: Padding(
        padding: const EdgeInsets.all(14),
        child: Row(
          children: [
            // Icon
            ItemIcon(itemName: item.name, size: 52),
            const SizedBox(width: 14),

            // Name + qty + days
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
                    child: Text(daysLabel,
                        style: TextStyle(
                            fontSize: 11,
                            fontWeight: FontWeight.w700,
                            color: _badgeTextColor)),
                  ),
                ],
              ),
            ),

            // Add to shopping list button
            IconButton(
              onPressed: () {
                ScaffoldMessenger.of(context).showSnackBar(SnackBar(
                  content: Text('${item.displayName} added to shopping list'),
                  backgroundColor: AppColors.primary,
                ));
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
        border: Border.all(
            color: AppColors.secondary.withValues(alpha: 0.3)),
        boxShadow: const [
          BoxShadow(
              color: Color(0x08000000),
              blurRadius: 6,
              offset: Offset(0, 2)),
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
              padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 2),
              decoration: BoxDecoration(
                color: AppColors.secondaryContainer,
                borderRadius: BorderRadius.circular(20),
              ),
              child: const Text('7–14 days',
                  style: TextStyle(
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
