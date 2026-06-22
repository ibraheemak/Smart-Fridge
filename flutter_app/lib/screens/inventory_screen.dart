import 'package:flutter/material.dart';
import 'package:shimmer/shimmer.dart';
import '../models/fridge_item.dart';
import '../services/fridge_service.dart';
import '../theme/app_theme.dart';
import '../widgets/item_icon.dart';
import 'expiry_screen.dart';

class InventoryScreen extends StatefulWidget {
  const InventoryScreen({super.key});

  @override
  State<InventoryScreen> createState() => _InventoryScreenState();
}

class _InventoryScreenState extends State<InventoryScreen> {
  String _filter = 'All';
  String _search = '';

  final _filters = ['All', 'Fresh', 'Expiring Soon', 'Expired'];

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: AppColors.background,
      body: SafeArea(
        child: StreamBuilder<InventorySnapshot?>(
          stream: FridgeService.inventoryStream(),
          builder: (ctx, snap) {
            final isLoading =
                snap.connectionState == ConnectionState.waiting;
            final inv = snap.data;

            List<FridgeItem> items = inv?.items ?? [];

            // Filter
            if (_search.isNotEmpty) {
              items = items
                  .where((i) => i.name
                      .toLowerCase()
                      .contains(_search.toLowerCase()))
                  .toList();
            }
            if (_filter == 'Fresh') {
              items =
                  items.where((i) => i.confidence == 'high').toList();
            } else if (_filter == 'Expiring Soon') {
              items =
                  items.where((i) => i.confidence == 'medium').toList();
            } else if (_filter == 'Expired') {
              items =
                  items.where((i) => i.confidence == 'low').toList();
            }

            return CustomScrollView(
              slivers: [
                // ── App Bar ──────────────────────────────────────────────
                SliverAppBar(
                  floating: true,
                  backgroundColor: AppColors.background,
                  elevation: 0,
                  scrolledUnderElevation: 0,
                  titleSpacing: 16,
                  title: Text(
                    'Smart Fridge',
                    style: Theme.of(context).textTheme.headlineMedium,
                  ),
                  actions: [
                    IconButton(
                      onPressed: () {},
                      icon: const Icon(Icons.notifications_outlined,
                          color: AppColors.primary),
                    ),
                    Padding(
                      padding: const EdgeInsets.only(right: 16),
                      child: CircleAvatar(
                        radius: 18,
                        backgroundColor: AppColors.primaryFixed,
                        child: const Text(
                          'SF',
                          style: TextStyle(
                            fontSize: 12,
                            fontWeight: FontWeight.w700,
                            color: AppColors.primary,
                          ),
                        ),
                      ),
                    ),
                  ],
                ),

                SliverToBoxAdapter(
                  child: Padding(
                    padding: const EdgeInsets.fromLTRB(16, 8, 16, 0),
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        // ── Search ────────────────────────────────────────
                        Container(
                          height: 48,
                          decoration: BoxDecoration(
                            color: AppColors.surfaceContainerLow,
                            borderRadius: BorderRadius.circular(14),
                          ),
                          child: TextField(
                            onChanged: (v) =>
                                setState(() => _search = v),
                            decoration: InputDecoration(
                              hintText: 'Search fridge...',
                              prefixIcon: const Icon(Icons.search_rounded,
                                  color: AppColors.outline),
                              border: InputBorder.none,
                              contentPadding: const EdgeInsets.symmetric(
                                  vertical: 12),
                            ),
                          ),
                        ),
                        const SizedBox(height: 12),

                        // ── Filter chips ──────────────────────────────────
                        SizedBox(
                          height: 36,
                          child: ListView.separated(
                            scrollDirection: Axis.horizontal,
                            itemCount: _filters.length,
                            separatorBuilder: (_, __) =>
                                const SizedBox(width: 8),
                            itemBuilder: (_, i) {
                              final f = _filters[i];
                              final active = f == _filter;
                              return GestureDetector(
                                onTap: () =>
                                    setState(() => _filter = f),
                                child: AnimatedContainer(
                                  duration:
                                      const Duration(milliseconds: 180),
                                  padding: const EdgeInsets.symmetric(
                                      horizontal: 20, vertical: 6),
                                  decoration: BoxDecoration(
                                    color: active
                                        ? AppColors.primary
                                        : AppColors.surfaceContainerHigh,
                                    borderRadius:
                                        BorderRadius.circular(20),
                                  ),
                                  child: Text(
                                    f,
                                    style: TextStyle(
                                      fontSize: 12,
                                      fontWeight: FontWeight.w600,
                                      color: active
                                          ? AppColors.onPrimary
                                          : AppColors.onSurfaceVariant,
                                    ),
                                  ),
                                ),
                              );
                            },
                          ),
                        ),
                        const SizedBox(height: 12),

                        // ── Expiry Alerts Banner ──────────────────────────
                        if (inv != null)
                          Builder(builder: (ctx) {
                            final lowCount = inv.items
                                .where((i) => i.confidence == 'low')
                                .length;
                            if (lowCount == 0) return const SizedBox.shrink();
                            return GestureDetector(
                              onTap: () => Navigator.push(
                                  context,
                                  MaterialPageRoute(
                                      builder: (_) => const ExpiryScreen())),
                              child: Container(
                                margin: const EdgeInsets.only(bottom: 12),
                                padding: const EdgeInsets.all(12),
                                decoration: BoxDecoration(
                                  color: AppColors.errorContainer,
                                  borderRadius: BorderRadius.circular(12),
                                ),
                                child: Row(
                                  children: [
                                    const Icon(Icons.warning_amber_rounded,
                                        color: AppColors.error, size: 20),
                                    const SizedBox(width: 8),
                                    Expanded(
                                      child: Text(
                                        '$lowCount item${lowCount == 1 ? '' : 's'} need attention — check expiry',
                                        style: const TextStyle(
                                            fontSize: 13,
                                            fontWeight: FontWeight.w600,
                                            color: AppColors.error),
                                      ),
                                    ),
                                    const Icon(Icons.chevron_right_rounded,
                                        color: AppColors.error),
                                  ],
                                ),
                              ),
                            );
                          }),

                        const SizedBox(height: 4),
                      ],
                    ),
                  ),
                ),

                // ── Loading skeleton ─────────────────────────────────────
                if (isLoading)
                  SliverPadding(
                    padding:
                        const EdgeInsets.symmetric(horizontal: 16),
                    sliver: SliverGrid(
                      gridDelegate:
                          const SliverGridDelegateWithFixedCrossAxisCount(
                        crossAxisCount: 2,
                        mainAxisSpacing: 16,
                        crossAxisSpacing: 16,
                        childAspectRatio: 0.78,
                      ),
                      delegate: SliverChildBuilderDelegate(
                        (_, __) => _SkeletonCard(),
                        childCount: 6,
                      ),
                    ),
                  )

                // ── Empty state ──────────────────────────────────────────
                else if (items.isEmpty)
                  SliverFillRemaining(
                    child: Center(
                      child: Padding(
                        padding: const EdgeInsets.all(32),
                        child: Column(
                          mainAxisSize: MainAxisSize.min,
                          children: [
                            Icon(
                              Icons.kitchen_rounded,
                              size: 72,
                              color: AppColors.outline,
                            ),
                            const SizedBox(height: 16),
                            Text(
                              inv == null
                                  ? 'Fridge is empty'
                                  : 'No items match',
                              style: Theme.of(context)
                                  .textTheme
                                  .titleLarge,
                            ),
                            const SizedBox(height: 8),
                            Text(
                              inv == null
                                  ? 'Close the door to trigger an auto-scan'
                                  : 'Try a different search or filter',
                              textAlign: TextAlign.center,
                              style: Theme.of(context)
                                  .textTheme
                                  .bodyMedium,
                            ),
                          ],
                        ),
                      ),
                    ),
                  )

                // ── Bento grid ───────────────────────────────────────────
                else
                  SliverPadding(
                    padding:
                        const EdgeInsets.fromLTRB(16, 0, 16, 100),
                    sliver: SliverGrid(
                      gridDelegate:
                          const SliverGridDelegateWithFixedCrossAxisCount(
                        crossAxisCount: 2,
                        mainAxisSpacing: 16,
                        crossAxisSpacing: 16,
                        childAspectRatio: 0.75,
                      ),
                      delegate: SliverChildBuilderDelegate(
                        (_, i) => _InventoryCard(item: items[i]),
                        childCount: items.length,
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

class _InventoryCard extends StatelessWidget {
  final FridgeItem item;

  const _InventoryCard({required this.item});

  Color get _borderColor {
    switch (item.confidence) {
      case 'high':
        return AppColors.secondary;
      case 'medium':
        return AppColors.tertiaryFixedDim;
      default:
        return AppColors.error;
    }
  }

  String get _statusLabel {
    switch (item.confidence) {
      case 'high':
        return 'Fresh';
      case 'medium':
        return 'Expiring Soon';
      default:
        return 'Check';
    }
  }

  Color get _statusColor {
    switch (item.confidence) {
      case 'high':
        return AppColors.secondary;
      case 'medium':
        return AppColors.tertiaryFixedDim;
      default:
        return AppColors.error;
    }
  }

  @override
  Widget build(BuildContext context) {
    return Container(
      decoration: BoxDecoration(
        color: AppColors.surfaceContainerLowest,
        borderRadius: BorderRadius.circular(20),
        border: Border(top: BorderSide(color: _borderColor, width: 4)),
        boxShadow: const [
          BoxShadow(
              color: Color(0x0A000000),
              blurRadius: 8,
              offset: Offset(0, 2)),
        ],
      ),
      child: Padding(
        padding: const EdgeInsets.fromLTRB(12, 12, 12, 12),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            // Image — tries Firebase Storage, then generates with Gemini AI
            Expanded(
              child: LayoutBuilder(
                builder: (_, constraints) => ItemIcon(
                  itemName: item.name,
                  size: constraints.maxWidth,
                  borderRadius: BorderRadius.circular(12),
                ),
              ),
            ),
            const SizedBox(height: 10),

            // Name
            Text(
              item.displayName,
              style: Theme.of(context).textTheme.titleMedium?.copyWith(
                    fontWeight: FontWeight.w700,
                  ),
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
            ),
            const SizedBox(height: 4),

            // Quantity + Status
            Row(
              children: [
                Expanded(
                  child: Text(
                    'Qty: ${item.quantity}',
                    style: Theme.of(context).textTheme.bodyMedium,
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                  ),
                ),
                Text(
                  _statusLabel,
                  style: TextStyle(
                    fontSize: 12,
                    fontWeight: FontWeight.w700,
                    color: _statusColor,
                  ),
                ),
              ],
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
      baseColor: AppColors.surfaceContainerHigh,
      highlightColor: AppColors.surfaceContainerLow,
      child: Container(
        decoration: BoxDecoration(
          color: AppColors.surfaceContainerHigh,
          borderRadius: BorderRadius.circular(20),
        ),
      ),
    );
  }
}
