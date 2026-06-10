import 'package:flutter/material.dart';
import 'package:shimmer/shimmer.dart';
import '../models/fridge_item.dart';
import '../services/fridge_service.dart';
import '../theme/app_theme.dart';
import '../widgets/item_card.dart';
import '../widgets/temperature_card.dart';

class InventoryScreen extends StatelessWidget {
  const InventoryScreen({super.key});

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: SafeArea(
        child: CustomScrollView(
          slivers: [
            // ── Header ──────────────────────────────────────────────────────
            SliverToBoxAdapter(
              child: Padding(
                padding: const EdgeInsets.fromLTRB(20, 24, 20, 12),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Row(
                      children: [
                        const Text('🧊',
                            style: TextStyle(fontSize: 30)),
                        const SizedBox(width: 10),
                        Column(
                          crossAxisAlignment: CrossAxisAlignment.start,
                          children: [
                            Text(
                              'Smart Fridge',
                              style: Theme.of(context)
                                  .textTheme
                                  .headlineMedium,
                            ),
                            StreamBuilder<InventorySnapshot?>(
                              stream: FridgeService.inventoryStream(),
                              builder: (_, s) {
                                final ts = s.data?.updatedAt ?? '';
                                return Text(
                                  ts.isNotEmpty
                                      ? 'Last scan: $ts'
                                      : 'Waiting for first scan…',
                                  style: const TextStyle(
                                    fontSize: 12,
                                    color: AppColors.textSecondary,
                                  ),
                                );
                              },
                            ),
                          ],
                        ),
                      ],
                    ),
                    const SizedBox(height: 16),
                    const TemperatureCard(),
                    const SizedBox(height: 8),
                  ],
                ),
              ),
            ),

            // ── Inventory grid ───────────────────────────────────────────────
            StreamBuilder<InventorySnapshot?>(
              stream: FridgeService.inventoryStream(),
              builder: (context, snap) {
                // Loading skeleton
                if (snap.connectionState == ConnectionState.waiting) {
                  return SliverPadding(
                    padding: const EdgeInsets.symmetric(horizontal: 16),
                    sliver: SliverGrid(
                      gridDelegate:
                          const SliverGridDelegateWithFixedCrossAxisCount(
                        crossAxisCount: 2,
                        mainAxisSpacing: 12,
                        crossAxisSpacing: 12,
                        childAspectRatio: 0.85,
                      ),
                      delegate: SliverChildBuilderDelegate(
                        (_, __) => _SkeletonCard(),
                        childCount: 6,
                      ),
                    ),
                  );
                }

                final inv = snap.data;

                // Empty state
                if (inv == null || inv.items.isEmpty) {
                  return SliverFillRemaining(
                    child: Center(
                      child: Padding(
                        padding: const EdgeInsets.all(32),
                        child: Column(
                          mainAxisSize: MainAxisSize.min,
                          children: [
                            const Text('🍽️',
                                style: TextStyle(fontSize: 72)),
                            const SizedBox(height: 16),
                            Text(
                              'Fridge is empty',
                              style: Theme.of(context).textTheme.titleLarge,
                            ),
                            const SizedBox(height: 8),
                            const Text(
                              'Close the fridge door to trigger an\nauto-scan, or wait for the next one.',
                              textAlign: TextAlign.center,
                              style: TextStyle(
                                color: AppColors.textSecondary,
                                height: 1.5,
                              ),
                            ),
                          ],
                        ),
                      ),
                    ),
                  );
                }

                // Item count badge
                return SliverMainAxisGroup(slivers: [
                  SliverToBoxAdapter(
                    child: Padding(
                      padding:
                          const EdgeInsets.fromLTRB(20, 0, 20, 10),
                      child: Text(
                        '${inv.items.length} items',
                        style: const TextStyle(
                          fontSize: 13,
                          fontWeight: FontWeight.w600,
                          color: AppColors.textSecondary,
                        ),
                      ),
                    ),
                  ),
                  SliverPadding(
                    padding:
                        const EdgeInsets.fromLTRB(16, 0, 16, 100),
                    sliver: SliverGrid(
                      gridDelegate:
                          const SliverGridDelegateWithFixedCrossAxisCount(
                        crossAxisCount: 2,
                        mainAxisSpacing: 12,
                        crossAxisSpacing: 12,
                        childAspectRatio: 0.85,
                      ),
                      delegate: SliverChildBuilderDelegate(
                        (_, i) => ItemCard(item: inv.items[i]),
                        childCount: inv.items.length,
                      ),
                    ),
                  ),
                ]);
              },
            ),
          ],
        ),
      ),
    );
  }
}

class _SkeletonCard extends StatelessWidget {
  @override
  Widget build(BuildContext context) {
    return Shimmer.fromColors(
      baseColor: const Color(0xFFEEE6DD),
      highlightColor: AppColors.background,
      child: Container(
        decoration: BoxDecoration(
          color: AppColors.card,
          borderRadius: BorderRadius.circular(16),
        ),
      ),
    );
  }
}
