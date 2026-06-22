import 'package:flutter/material.dart';
import '../models/fridge_item.dart';
import '../services/fridge_service.dart';
import '../services/gemini_service.dart';
import '../theme/app_theme.dart';

const _staples = [
  'milk', 'eggs', 'butter', 'cheese', 'yogurt',
  'bread', 'chicken', 'beef', 'fish',
  'vegetables', 'fruits', 'juice',
];

class ShoppingScreen extends StatefulWidget {
  const ShoppingScreen({super.key});

  @override
  State<ShoppingScreen> createState() => _ShoppingScreenState();
}

class _ShoppingScreenState extends State<ShoppingScreen> {
  final Set<String> _checked = {};
  final List<String> _manual = [];
  final _addCtrl = TextEditingController();
  bool _aiLoading = false;

  void _toggle(String name) => setState(() =>
      _checked.contains(name) ? _checked.remove(name) : _checked.add(name));

  void _addItem() {
    final text = _addCtrl.text.trim();
    if (text.isEmpty) return;
    setState(() {
      _manual.add(text);
      _addCtrl.clear();
    });
  }

  Future<void> _autoSuggest(List<FridgeItem> items) async {
    setState(() => _aiLoading = true);
    try {
      final inv = await FridgeService.inventoryStream().first;
      if (inv == null) return;
      final recipes = await GeminiService.getRecipes(inv.items);
      // Extract ingredients not already in manual or checked
      final all = <String>{};
      for (final r in recipes) {
        for (final ing in r.ingredients) {
          final key = ing.toLowerCase();
          if (!_manual.any((m) => m.toLowerCase() == key) &&
              !_staples.contains(key)) {
            all.add(ing);
          }
        }
      }
      if (all.isNotEmpty) {
        setState(() => _manual.addAll(all.take(5)));
        if (mounted) {
          ScaffoldMessenger.of(context).showSnackBar(
            SnackBar(
              content: Text('Added ${all.take(5).length} AI suggestions'),
              backgroundColor: AppColors.primary,
            ),
          );
        }
      }
    } catch (_) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(
              content: Text('AI suggestion unavailable'),
              backgroundColor: AppColors.error),
        );
      }
    } finally {
      if (mounted) setState(() => _aiLoading = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: AppColors.background,
      body: SafeArea(
        child: StreamBuilder<InventorySnapshot?>(
          stream: FridgeService.inventoryStream(),
          builder: (ctx, snap) {
            final inFridge = snap.data?.items
                    .map((i) => i.name.toLowerCase())
                    .toSet() ??
                {};
            final missing =
                _staples.where((s) => !inFridge.contains(s)).toList();
            final lowConf = snap.data?.items
                    .where((i) => i.confidence == 'low')
                    .map((i) => i.name)
                    .toList() ??
                [];

            // Budget estimate (~₪15 per staple, ₪20 per AI item)
            final uncheckedMissing =
                missing.where((n) => !_checked.contains(n)).length;
            final uncheckedManual =
                _manual.where((n) => !_checked.contains(n)).length;
            final budget =
                (uncheckedMissing * 15) + (uncheckedManual * 20) + (lowConf.where((n) => !_checked.contains(n)).length * 12);

            return CustomScrollView(
              slivers: [
                // ── App Bar ────────────────────────────────────────────────
                SliverAppBar(
                  floating: true,
                  backgroundColor: AppColors.background,
                  elevation: 0,
                  scrolledUnderElevation: 0,
                  titleSpacing: 16,
                  title: Text('Smart Fridge',
                      style: Theme.of(context).textTheme.headlineMedium),
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
                        child: const Text('SF',
                            style: TextStyle(
                                fontSize: 12,
                                fontWeight: FontWeight.w700,
                                color: AppColors.primary)),
                      ),
                    ),
                  ],
                ),

                SliverToBoxAdapter(
                  child: Padding(
                    padding: const EdgeInsets.fromLTRB(16, 8, 16, 4),
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Text(
                          'Shopping List',
                          style: Theme.of(context)
                              .textTheme
                              .titleLarge
                              ?.copyWith(fontWeight: FontWeight.w700),
                        ),
                        const SizedBox(height: 4),
                        Text(
                          'Based on your current inventory',
                          style: Theme.of(context).textTheme.bodyMedium,
                        ),
                        const SizedBox(height: 16),

                        // ── Add item input ─────────────────────────────────
                        Row(
                          children: [
                            Expanded(
                              child: TextField(
                                controller: _addCtrl,
                                textInputAction: TextInputAction.done,
                                onSubmitted: (_) => _addItem(),
                                decoration: InputDecoration(
                                  hintText: 'Add item to list...',
                                  prefixIcon: const Icon(
                                      Icons.add_shopping_cart_rounded,
                                      color: AppColors.primary),
                                  filled: true,
                                  fillColor: AppColors.surfaceContainerLowest,
                                  border: OutlineInputBorder(
                                    borderRadius: BorderRadius.circular(14),
                                    borderSide: BorderSide.none,
                                  ),
                                  contentPadding: const EdgeInsets.symmetric(
                                      horizontal: 16, vertical: 14),
                                ),
                              ),
                            ),
                            const SizedBox(width: 8),
                            FilledButton(
                              onPressed: _addItem,
                              style: FilledButton.styleFrom(
                                  padding: const EdgeInsets.symmetric(
                                      horizontal: 16, vertical: 16),
                                  shape: RoundedRectangleBorder(
                                      borderRadius: BorderRadius.circular(14))),
                              child: const Text('Add'),
                            ),
                          ],
                        ),
                        const SizedBox(height: 10),

                        // ── Action buttons row ─────────────────────────────
                        Row(
                          children: [
                            Expanded(
                              child: OutlinedButton.icon(
                                onPressed: _aiLoading
                                    ? null
                                    : () => _autoSuggest(snap.data?.items ?? []),
                                icon: _aiLoading
                                    ? const SizedBox(
                                        width: 14,
                                        height: 14,
                                        child: CircularProgressIndicator(
                                            strokeWidth: 2,
                                            color: AppColors.primary))
                                    : const Icon(Icons.auto_awesome_rounded,
                                        size: 16),
                                label: const Text('AI Suggestions'),
                                style: OutlinedButton.styleFrom(
                                  foregroundColor: AppColors.primary,
                                  side: const BorderSide(
                                      color: AppColors.primary),
                                  shape: RoundedRectangleBorder(
                                      borderRadius: BorderRadius.circular(12)),
                                ),
                              ),
                            ),
                            const SizedBox(width: 10),
                            Expanded(
                              child: OutlinedButton.icon(
                                onPressed: () {
                                  ScaffoldMessenger.of(context)
                                      .showSnackBar(const SnackBar(
                                    content: Text('Sharing list...'),
                                  ));
                                },
                                icon: const Icon(Icons.share_rounded, size: 16),
                                label: const Text('Share List'),
                                style: OutlinedButton.styleFrom(
                                  foregroundColor: AppColors.onSurfaceVariant,
                                  side: const BorderSide(
                                      color: AppColors.outlineVariant),
                                  shape: RoundedRectangleBorder(
                                      borderRadius: BorderRadius.circular(12)),
                                ),
                              ),
                            ),
                          ],
                        ),
                        const SizedBox(height: 14),

                        // ── Stats row ──────────────────────────────────────
                        Row(
                          children: [
                            Expanded(
                              child: _StatChip(
                                icon: Icons.checklist_rounded,
                                label: 'Remaining',
                                value:
                                    '${uncheckedMissing + uncheckedManual + lowConf.where((n) => !_checked.contains(n)).length}',
                                color: AppColors.primary,
                              ),
                            ),
                            const SizedBox(width: 12),
                            Expanded(
                              child: _StatChip(
                                icon: Icons.account_balance_wallet_rounded,
                                label: 'Est. Budget',
                                value: '₪$budget',
                                color: AppColors.secondary,
                              ),
                            ),
                          ],
                        ),
                        const SizedBox(height: 4),
                      ],
                    ),
                  ),
                ),

                SliverPadding(
                  padding: const EdgeInsets.fromLTRB(16, 8, 16, 100),
                  sliver: SliverList(
                    delegate: SliverChildListDelegate([
                      // ── Manual items ─────────────────────────────────────
                      if (_manual.isNotEmpty) ...[
                        _SectionHeader(
                          icon: Icons.edit_rounded,
                          color: AppColors.primary,
                          label: 'My List',
                          count: _manual.length,
                        ),
                        const SizedBox(height: 10),
                        ..._manual.map((n) => _ShoppingItem(
                              name: _cap(n),
                              checked: _checked.contains(n),
                              onTap: () => _toggle(n),
                              onDelete: () =>
                                  setState(() => _manual.remove(n)),
                              accentColor: AppColors.primary,
                            )),
                        const SizedBox(height: 20),
                      ],

                      // ── Low confidence section ───────────────────────────
                      if (lowConf.isNotEmpty) ...[
                        _SectionHeader(
                          icon: Icons.warning_amber_rounded,
                          color: AppColors.tertiaryFixedDim,
                          label: 'Possibly Needed',
                          count: lowConf.length,
                        ),
                        const SizedBox(height: 10),
                        ...lowConf.map((n) => _ShoppingItem(
                              name: _cap(n),
                              subtitle: 'Detected but uncertain — verify',
                              checked: _checked.contains(n),
                              onTap: () => _toggle(n),
                              accentColor: AppColors.tertiaryFixedDim,
                            )),
                        const SizedBox(height: 20),
                      ],

                      // ── Missing section ──────────────────────────────────
                      if (missing.isNotEmpty) ...[
                        _SectionHeader(
                          icon: Icons.remove_shopping_cart_rounded,
                          color: AppColors.error,
                          label: 'Not in Fridge',
                          count: missing.length,
                        ),
                        const SizedBox(height: 10),
                        ...missing.map((n) => _ShoppingItem(
                              name: _cap(n),
                              checked: _checked.contains(n),
                              onTap: () => _toggle(n),
                              accentColor: AppColors.primary,
                            )),
                      ],

                      if (missing.isEmpty && lowConf.isEmpty && _manual.isEmpty)
                        Padding(
                          padding: const EdgeInsets.symmetric(vertical: 60),
                          child: Center(
                            child: Column(
                              children: [
                                Container(
                                  width: 72,
                                  height: 72,
                                  decoration: const BoxDecoration(
                                    color: AppColors.secondaryContainer,
                                    shape: BoxShape.circle,
                                  ),
                                  child: const Icon(Icons.check_circle_rounded,
                                      size: 40,
                                      color: AppColors.onSecondaryContainer),
                                ),
                                const SizedBox(height: 16),
                                Text('All stocked up!',
                                    style:
                                        Theme.of(context).textTheme.titleLarge),
                                const SizedBox(height: 8),
                                const Text('Nothing missing from the fridge.',
                                    style: TextStyle(
                                        color: AppColors.onSurfaceVariant)),
                              ],
                            ),
                          ),
                        ),
                    ]),
                  ),
                ),
              ],
            );
          },
        ),
      ),
    );
  }

  static String _cap(String s) =>
      s.isEmpty ? s : s[0].toUpperCase() + s.substring(1);

  @override
  void dispose() {
    _addCtrl.dispose();
    super.dispose();
  }
}

class _StatChip extends StatelessWidget {
  final IconData icon;
  final String label;
  final String value;
  final Color color;

  const _StatChip(
      {required this.icon,
      required this.label,
      required this.value,
      required this.color});

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.all(12),
      decoration: BoxDecoration(
        color: color.withValues(alpha: 0.1),
        borderRadius: BorderRadius.circular(14),
        border: Border.all(color: color.withValues(alpha: 0.2)),
      ),
      child: Row(
        children: [
          Icon(icon, size: 20, color: color),
          const SizedBox(width: 10),
          Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(value,
                  style: TextStyle(
                      fontSize: 18,
                      fontWeight: FontWeight.w800,
                      color: color)),
              Text(label,
                  style: const TextStyle(
                      fontSize: 10,
                      color: AppColors.onSurfaceVariant)),
            ],
          ),
        ],
      ),
    );
  }
}

class _SectionHeader extends StatelessWidget {
  final IconData icon;
  final Color color;
  final String label;
  final int count;

  const _SectionHeader({
    required this.icon,
    required this.color,
    required this.label,
    required this.count,
  });

  @override
  Widget build(BuildContext context) {
    return Row(
      children: [
        Icon(icon, size: 16, color: color),
        const SizedBox(width: 8),
        Text(
          label,
          style: TextStyle(
            fontSize: 14,
            fontWeight: FontWeight.w700,
            color: AppColors.onSurface,
          ),
        ),
        const Spacer(),
        Container(
          padding:
              const EdgeInsets.symmetric(horizontal: 10, vertical: 3),
          decoration: BoxDecoration(
            color: color.withValues(alpha: 0.12),
            borderRadius: BorderRadius.circular(20),
          ),
          child: Text(
            '$count items',
            style: TextStyle(
              fontSize: 11,
              fontWeight: FontWeight.w700,
              color: color,
            ),
          ),
        ),
      ],
    );
  }
}

class _ShoppingItem extends StatelessWidget {
  final String name;
  final String? subtitle;
  final bool checked;
  final VoidCallback onTap;
  final VoidCallback? onDelete;
  final Color accentColor;

  const _ShoppingItem({
    required this.name,
    this.subtitle,
    required this.checked,
    required this.onTap,
    this.onDelete,
    required this.accentColor,
  });

  @override
  Widget build(BuildContext context) {
    return GestureDetector(
      onTap: onTap,
      child: Container(
        margin: const EdgeInsets.only(bottom: 10),
        padding:
            const EdgeInsets.symmetric(horizontal: 16, vertical: 14),
        decoration: BoxDecoration(
          color: AppColors.surfaceContainerLowest,
          borderRadius: BorderRadius.circular(14),
          border: Border(
              left: BorderSide(
                  color: checked
                      ? AppColors.outlineVariant
                      : accentColor,
                  width: 3)),
          boxShadow: const [
            BoxShadow(
                color: Color(0x08000000),
                blurRadius: 6,
                offset: Offset(0, 2)),
          ],
        ),
        child: Row(
          children: [
            // Checkbox
            AnimatedContainer(
              duration: const Duration(milliseconds: 200),
              width: 22,
              height: 22,
              decoration: BoxDecoration(
                color: checked ? AppColors.primary : Colors.transparent,
                border: Border.all(
                  color:
                      checked ? AppColors.primary : AppColors.outline,
                  width: 2,
                ),
                borderRadius: BorderRadius.circular(6),
              ),
              child: checked
                  ? const Icon(Icons.check_rounded,
                      size: 14, color: Colors.white)
                  : null,
            ),
            const SizedBox(width: 14),

            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    name,
                    style: TextStyle(
                      fontSize: 15,
                      fontWeight: FontWeight.w600,
                      color: checked
                          ? AppColors.outline
                          : AppColors.onSurface,
                      decoration: checked
                          ? TextDecoration.lineThrough
                          : null,
                    ),
                  ),
                  if (subtitle != null)
                    Text(
                      subtitle!,
                      style: const TextStyle(
                          fontSize: 12,
                          color: AppColors.onSurfaceVariant),
                    ),
                ],
              ),
            ),

            if (onDelete != null)
              IconButton(
                onPressed: onDelete,
                icon: Icon(Icons.close_rounded,
                    size: 16, color: AppColors.outline),
                constraints: const BoxConstraints(
                    minWidth: 32, minHeight: 32),
                padding: EdgeInsets.zero,
              )
            else
              Icon(
                Icons.shopping_cart_outlined,
                size: 18,
                color: checked
                    ? AppColors.outlineVariant
                    : AppColors.onSurfaceVariant,
              ),
          ],
        ),
      ),
    );
  }
}
