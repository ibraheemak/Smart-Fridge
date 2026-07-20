import 'package:flutter/material.dart';
import 'package:shimmer/shimmer.dart';
import '../models/fridge_item.dart';
import '../services/fridge_service.dart';
import '../theme/app_theme.dart';
import '../widgets/app_top_bar.dart';
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

  final _filters = ['All', 'Fresh', 'Expiring Soon', 'Expired', 'No Date'];

  // Held in state so typing in the search box doesn't re-subscribe and
  // flash the loading skeleton on every keystroke.
  late final Stream<InventorySnapshot?> _invStream =
      FridgeService.inventoryStream();

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: AppColors.background,
      body: SafeArea(
        child: StreamBuilder<InventorySnapshot?>(
          stream: _invStream,
          builder: (ctx, snap) {
            final isLoading =
                snap.connectionState == ConnectionState.waiting;
            final inv = snap.data;

            List<FridgeItem> items = inv?.items ?? [];

            // Search
            if (_search.isNotEmpty) {
              items = items
                  .where((i) => i.name
                      .toLowerCase()
                      .contains(_search.toLowerCase()))
                  .toList();
            }

            // Filter by expiry status
            if (_filter == 'Fresh') {
              items = items
                  .where((i) => i.expiryStatus == ExpiryStatus.ok)
                  .toList();
            } else if (_filter == 'Expiring Soon') {
              items = items
                  .where((i) =>
                      i.expiryStatus == ExpiryStatus.critical ||
                      i.expiryStatus == ExpiryStatus.soon)
                  .toList();
            } else if (_filter == 'Expired') {
              items = items
                  .where((i) => i.expiryStatus == ExpiryStatus.expired)
                  .toList();
            } else if (_filter == 'No Date') {
              items = items
                  .where((i) => i.expiryStatus == ExpiryStatus.unknown)
                  .toList();
            }

            return CustomScrollView(
              slivers: [
                // ── App Bar ──────────────────────────────────────────────
                const AppTopBar(),

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
                            decoration: const InputDecoration(
                              hintText: 'Search fridge...',
                              prefixIcon: Icon(Icons.search_rounded,
                                  color: AppColors.outline),
                              border: InputBorder.none,
                              contentPadding: EdgeInsets.symmetric(
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
                            final alertCount = inv.items
                                .where((i) =>
                                    i.expiryStatus == ExpiryStatus.expired ||
                                    i.expiryStatus == ExpiryStatus.critical ||
                                    i.expiryStatus == ExpiryStatus.soon)
                                .length;
                            if (alertCount == 0) return const SizedBox.shrink();
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
                                        '$alertCount item${alertCount == 1 ? '' : 's'} expiring soon — tap to view',
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
                            const Icon(
                              Icons.kitchen_rounded,
                              size: 72,
                              color: AppColors.outline,
                            ),
                            const SizedBox(height: 16),
                            Builder(builder: (context) {
                              // "No items match" only makes sense when a
                              // search or filter is hiding items that exist.
                              final filtering = _search.isNotEmpty ||
                                  _filter != 'All';
                              final fridgeEmpty =
                                  (inv?.items.isEmpty ?? true) || !filtering;
                              return Column(
                                children: [
                                  Text(
                                    fridgeEmpty
                                        ? 'Fridge is empty'
                                        : 'No items match',
                                    style: Theme.of(context)
                                        .textTheme
                                        .titleLarge,
                                  ),
                                  const SizedBox(height: 8),
                                  Text(
                                    fridgeEmpty
                                        ? 'Close the door to trigger an auto-scan'
                                        : 'Try a different search or filter',
                                    textAlign: TextAlign.center,
                                    style: Theme.of(context)
                                        .textTheme
                                        .bodyMedium,
                                  ),
                                ],
                              );
                            }),
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
                        childAspectRatio: 0.70,
                      ),
                      delegate: SliverChildBuilderDelegate(
                        (_, i) => _InventoryCard(
                          item: items[i],
                          onTap: () => _showItemSheet(context, items[i]),
                        ),
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

  void _showItemSheet(BuildContext context, FridgeItem item) {
    showModalBottomSheet(
      context: context,
      backgroundColor: AppColors.surfaceContainerLowest,
      shape: const RoundedRectangleBorder(
        borderRadius: BorderRadius.vertical(top: Radius.circular(24)),
      ),
      builder: (_) => _ItemActionsSheet(item: item),
    );
  }
}

// ── Item actions bottom sheet: adjust quantity / remove ───────────────────────
class _ItemActionsSheet extends StatefulWidget {
  final FridgeItem item;

  const _ItemActionsSheet({required this.item});

  @override
  State<_ItemActionsSheet> createState() => _ItemActionsSheetState();
}

class _ItemActionsSheetState extends State<_ItemActionsSheet> {
  late int _qty;
  bool _busy = false;

  @override
  void initState() {
    super.initState();
    _qty = int.tryParse(widget.item.quantity) ?? 1;
    if (_qty < 1) _qty = 1;
  }

  Future<void> _save() async {
    setState(() => _busy = true);
    await FridgeService.setInventoryQuantity(widget.item.name, _qty);
    if (mounted) Navigator.pop(context);
  }

  Future<void> _remove() async {
    setState(() => _busy = true);
    await FridgeService.removeInventoryItem(widget.item.name);
    if (!mounted) return;
    Navigator.pop(context);
    ScaffoldMessenger.of(context).showSnackBar(
      SnackBar(
        content: Text('${widget.item.displayName} removed from fridge'),
        backgroundColor: AppColors.onSurface,
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    final changed = _qty != (int.tryParse(widget.item.quantity) ?? _qty);
    return SafeArea(
      child: Padding(
        padding: const EdgeInsets.fromLTRB(20, 12, 20, 20),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Center(
              child: Container(
                width: 40,
                height: 4,
                decoration: BoxDecoration(
                  color: AppColors.outlineVariant,
                  borderRadius: BorderRadius.circular(2),
                ),
              ),
            ),
            const SizedBox(height: 20),

            // Header
            Row(
              children: [
                ItemIcon(itemName: widget.item.name, size: 48),
                const SizedBox(width: 14),
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(widget.item.displayName,
                          style: Theme.of(context)
                              .textTheme
                              .titleLarge
                              ?.copyWith(fontWeight: FontWeight.w700)),
                      Text(widget.item.expiryLabel,
                          style: const TextStyle(
                              fontSize: 12,
                              color: AppColors.onSurfaceVariant)),
                    ],
                  ),
                ),
              ],
            ),
            const SizedBox(height: 24),

            // Quantity stepper
            Row(
              mainAxisAlignment: MainAxisAlignment.spaceBetween,
              children: [
                const Text('Quantity',
                    style: TextStyle(
                        fontSize: 15, fontWeight: FontWeight.w600)),
                Row(
                  children: [
                    _QtyButton(
                      icon: Icons.remove_rounded,
                      onTap: _qty > 1 && !_busy
                          ? () => setState(() => _qty--)
                          : null,
                    ),
                    Padding(
                      padding:
                          const EdgeInsets.symmetric(horizontal: 18),
                      child: Text('$_qty',
                          style: const TextStyle(
                              fontSize: 20,
                              fontWeight: FontWeight.w800)),
                    ),
                    _QtyButton(
                      icon: Icons.add_rounded,
                      onTap: !_busy ? () => setState(() => _qty++) : null,
                    ),
                  ],
                ),
              ],
            ),
            const SizedBox(height: 20),

            // Save quantity (only when changed)
            if (changed)
              SizedBox(
                width: double.infinity,
                child: FilledButton(
                  onPressed: _busy ? null : _save,
                  style: FilledButton.styleFrom(
                      padding: const EdgeInsets.symmetric(vertical: 14),
                      shape: RoundedRectangleBorder(
                          borderRadius: BorderRadius.circular(12))),
                  child: Text('Update quantity to $_qty'),
                ),
              ),
            if (changed) const SizedBox(height: 10),

            // Remove
            SizedBox(
              width: double.infinity,
              child: OutlinedButton.icon(
                onPressed: _busy ? null : _remove,
                icon: const Icon(Icons.delete_outline_rounded,
                    color: AppColors.error),
                label: const Text('Remove from fridge',
                    style: TextStyle(color: AppColors.error)),
                style: OutlinedButton.styleFrom(
                  side: const BorderSide(color: AppColors.error),
                  padding: const EdgeInsets.symmetric(vertical: 14),
                  shape: RoundedRectangleBorder(
                      borderRadius: BorderRadius.circular(12)),
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }
}

class _QtyButton extends StatelessWidget {
  final IconData icon;
  final VoidCallback? onTap;

  const _QtyButton({required this.icon, this.onTap});

  @override
  Widget build(BuildContext context) {
    final enabled = onTap != null;
    return GestureDetector(
      onTap: onTap,
      child: Container(
        width: 40,
        height: 40,
        decoration: BoxDecoration(
          color: enabled
              ? AppColors.primaryFixed
              : AppColors.surfaceContainerHigh,
          borderRadius: BorderRadius.circular(12),
        ),
        child: Icon(icon,
            size: 20,
            color: enabled ? AppColors.primary : AppColors.outline),
      ),
    );
  }
}

class _InventoryCard extends StatelessWidget {
  final FridgeItem item;
  final VoidCallback onTap;

  const _InventoryCard({required this.item, required this.onTap});

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

  @override
  Widget build(BuildContext context) {
    return GestureDetector(
      onTap: onTap,
      child: Container(
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
            // Image
            Expanded(
              child: LayoutBuilder(
                builder: (_, constraints) => ItemIcon(
                  itemName: item.name,
                  size: constraints.maxWidth,
                  borderRadius: BorderRadius.circular(12),
                ),
              ),
            ),
            const SizedBox(height: 8),

            // Name
            Text(
              item.displayName,
              style: Theme.of(context).textTheme.titleMedium?.copyWith(
                    fontWeight: FontWeight.w700,
                  ),
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
            ),
            const SizedBox(height: 3),

            // Quantity
            Text(
              'Qty: ${item.quantity}',
              style: Theme.of(context).textTheme.bodyMedium,
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
            ),
            const SizedBox(height: 5),

            // Expiry badge
            Container(
              padding:
                  const EdgeInsets.symmetric(horizontal: 6, vertical: 3),
              decoration: BoxDecoration(
                color: _borderColor.withValues(alpha: 0.12),
                borderRadius: BorderRadius.circular(20),
              ),
              child: Text(
                item.expiryLabel,
                style: TextStyle(
                  fontSize: 10,
                  fontWeight: FontWeight.w700,
                  color: _borderColor,
                ),
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
              ),
            ),
          ],
        ),
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
