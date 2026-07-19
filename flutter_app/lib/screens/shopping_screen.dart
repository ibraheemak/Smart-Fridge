import 'package:flutter/material.dart';
import 'package:share_plus/share_plus.dart';
import '../models/fridge_item.dart';
import '../services/fridge_service.dart';
import '../services/gemini_service.dart';
import '../theme/app_theme.dart';
import '../widgets/app_top_bar.dart';

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
  // Local checked state for ephemeral "Not in Fridge" and "Possibly Needed" sections
  final Set<String> _checkedEphemeral = {};
  final _addCtrl = TextEditingController();
  bool _aiLoading = false;

  // Held in state so rebuilds don't re-subscribe mid-interaction.
  late final Stream<InventorySnapshot?> _invStream =
      FridgeService.inventoryStream();
  late final Stream<List<ShoppingItem>> _listStream =
      FridgeService.shoppingListStream();

  void _toggleEphemeral(String name) => setState(() =>
      _checkedEphemeral.contains(name)
          ? _checkedEphemeral.remove(name)
          : _checkedEphemeral.add(name));

  Future<void> _addItem() async {
    final text = _addCtrl.text.trim();
    if (text.isEmpty) return;
    _addCtrl.clear();
    await FridgeService.addShoppingItem(text);
  }

  Future<void> _autoSuggest() async {
    setState(() => _aiLoading = true);
    try {
      final inv = await FridgeService.inventoryStream().first;
      if (inv == null) return;
      final recipes = await GeminiService.getRecipes(inv.items);
      final suggestions = <String>[];
      for (final r in recipes) {
        for (final ing in r.ingredients) {
          final key = ing.toLowerCase().trim();
          if (!_staples.contains(key) && suggestions.length < 5) {
            suggestions.add(key);
          }
        }
      }
      for (final s in suggestions) {
        await FridgeService.addShoppingItem(s);
      }
      if (mounted && suggestions.isNotEmpty) {
        ScaffoldMessenger.of(context).showSnackBar(SnackBar(
          content: Text('Added ${suggestions.length} AI suggestions'),
          backgroundColor: AppColors.primary,
        ));
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

  void _shareList(
    List<ShoppingItem> myList,
    List<String> missing,
    List<String> lowConf,
  ) {
    final lines = <String>[];
    for (final item in myList.where((i) => !i.checked)) {
      lines.add('• ${_cap(item.name)}');
    }
    for (final name in missing.where((n) => !_checkedEphemeral.contains(n))) {
      lines.add('• ${_cap(name)} (staple)');
    }
    for (final name in lowConf.where((n) => !_checkedEphemeral.contains(n))) {
      lines.add('• ${_cap(name)} (possibly needed)');
    }
    if (lines.isEmpty) {
      ScaffoldMessenger.of(context).showSnackBar(const SnackBar(
        content: Text('Shopping list is empty or all checked off'),
      ));
      return;
    }
    Share.share('Shopping List:\n${lines.join('\n')}');
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: AppColors.background,
      body: SafeArea(
        child: StreamBuilder<InventorySnapshot?>(
          stream: _invStream,
          builder: (ctx, invSnap) {
            final inFridge = invSnap.data?.items
                    .map((i) => i.name.toLowerCase())
                    .toSet() ??
                {};
            final missing =
                _staples.where((s) => !inFridge.contains(s)).toList();
            final lowConf = invSnap.data?.items
                    .where((i) => i.confidence == 'low')
                    .map((i) => i.name)
                    .toList() ??
                [];

            return StreamBuilder<List<ShoppingItem>>(
              stream: _listStream,
              builder: (ctx, listSnap) {
                final myList = listSnap.data ?? [];

                final uncheckedCount = myList.where((i) => !i.checked).length +
                    missing.where((n) => !_checkedEphemeral.contains(n)).length +
                    lowConf
                        .where((n) => !_checkedEphemeral.contains(n))
                        .length;
                final checkedCount = myList.where((i) => i.checked).length +
                    _checkedEphemeral.length;

                return CustomScrollView(
                  slivers: [
                    // ── App Bar ────────────────────────────────────────────
                    const AppTopBar(),

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

                            // ── Add item input ─────────────────────────────
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
                                      fillColor:
                                          AppColors.surfaceContainerLowest,
                                      border: OutlineInputBorder(
                                        borderRadius:
                                            BorderRadius.circular(14),
                                        borderSide: BorderSide.none,
                                      ),
                                      contentPadding:
                                          const EdgeInsets.symmetric(
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
                                          borderRadius:
                                              BorderRadius.circular(14))),
                                  child: const Text('Add'),
                                ),
                              ],
                            ),
                            const SizedBox(height: 10),

                            // ── Action buttons row ─────────────────────────
                            Row(
                              children: [
                                Expanded(
                                  child: OutlinedButton.icon(
                                    onPressed: _aiLoading ? null : _autoSuggest,
                                    icon: _aiLoading
                                        ? const SizedBox(
                                            width: 14,
                                            height: 14,
                                            child: CircularProgressIndicator(
                                                strokeWidth: 2,
                                                color: AppColors.primary))
                                        : const Icon(
                                            Icons.auto_awesome_rounded,
                                            size: 16),
                                    label: const Text('AI Suggestions'),
                                    style: OutlinedButton.styleFrom(
                                      foregroundColor: AppColors.primary,
                                      side: const BorderSide(
                                          color: AppColors.primary),
                                      shape: RoundedRectangleBorder(
                                          borderRadius:
                                              BorderRadius.circular(12)),
                                    ),
                                  ),
                                ),
                                const SizedBox(width: 10),
                                Expanded(
                                  child: OutlinedButton.icon(
                                    onPressed: () =>
                                        _shareList(myList, missing, lowConf),
                                    icon: const Icon(Icons.share_rounded,
                                        size: 16),
                                    label: const Text('Share List'),
                                    style: OutlinedButton.styleFrom(
                                      foregroundColor:
                                          AppColors.onSurfaceVariant,
                                      side: const BorderSide(
                                          color: AppColors.outlineVariant),
                                      shape: RoundedRectangleBorder(
                                          borderRadius:
                                              BorderRadius.circular(12)),
                                    ),
                                  ),
                                ),
                              ],
                            ),
                            const SizedBox(height: 14),

                            // ── Stats row ──────────────────────────────────
                            Row(
                              children: [
                                Expanded(
                                  child: _StatChip(
                                    icon: Icons.checklist_rounded,
                                    label: 'Remaining',
                                    value: '$uncheckedCount',
                                    color: AppColors.primary,
                                  ),
                                ),
                                const SizedBox(width: 12),
                                Expanded(
                                  child: _StatChip(
                                    icon: Icons.check_circle_rounded,
                                    label: 'Checked Off',
                                    value: '$checkedCount',
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
                          // ── My List (Firestore-persisted) ─────────────────
                          if (myList.isNotEmpty) ...[
                            _SectionHeader(
                              icon: Icons.edit_rounded,
                              color: AppColors.primary,
                              label: 'My List',
                              count: myList.length,
                              onClear: myList.isEmpty
                                  ? null
                                  : () => FridgeService.clearShoppingList(),
                            ),
                            const SizedBox(height: 10),
                            ...myList.map((item) => _ShoppingItemTile(
                                  name: _cap(item.name),
                                  checked: item.checked,
                                  onTap: () => FridgeService.toggleShoppingItem(
                                      item.id, item.checked),
                                  onDelete: () =>
                                      FridgeService.removeShoppingItem(item.id),
                                  accentColor: AppColors.primary,
                                )),
                            const SizedBox(height: 20),
                          ],

                          // ── Low confidence section ───────────────────────
                          if (lowConf.isNotEmpty) ...[
                            _SectionHeader(
                              icon: Icons.warning_amber_rounded,
                              color: AppColors.tertiaryFixedDim,
                              label: 'Possibly Needed',
                              count: lowConf.length,
                            ),
                            const SizedBox(height: 10),
                            ...lowConf.map((n) => _ShoppingItemTile(
                                  name: _cap(n),
                                  subtitle: 'Detected but uncertain — verify',
                                  checked: _checkedEphemeral.contains(n),
                                  onTap: () => _toggleEphemeral(n),
                                  accentColor: AppColors.tertiaryFixedDim,
                                )),
                            const SizedBox(height: 20),
                          ],

                          // ── Missing staples section ──────────────────────
                          if (missing.isNotEmpty) ...[
                            _SectionHeader(
                              icon: Icons.remove_shopping_cart_rounded,
                              color: AppColors.error,
                              label: 'Not in Fridge',
                              count: missing.length,
                            ),
                            const SizedBox(height: 10),
                            ...missing.map((n) => _ShoppingItemTile(
                                  name: _cap(n),
                                  checked: _checkedEphemeral.contains(n),
                                  onTap: () => _toggleEphemeral(n),
                                  accentColor: AppColors.primary,
                                )),
                          ],

                          if (missing.isEmpty &&
                              lowConf.isEmpty &&
                              myList.isEmpty)
                            Padding(
                              padding:
                                  const EdgeInsets.symmetric(vertical: 60),
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
                                      child: const Icon(
                                          Icons.check_circle_rounded,
                                          size: 40,
                                          color:
                                              AppColors.onSecondaryContainer),
                                    ),
                                    const SizedBox(height: 16),
                                    Text('All stocked up!',
                                        style: Theme.of(context)
                                            .textTheme
                                            .titleLarge),
                                    const SizedBox(height: 8),
                                    const Text(
                                        'Nothing missing from the fridge.',
                                        style: TextStyle(
                                            color:
                                                AppColors.onSurfaceVariant)),
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
                      fontSize: 10, color: AppColors.onSurfaceVariant)),
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
  final VoidCallback? onClear;

  const _SectionHeader({
    required this.icon,
    required this.color,
    required this.label,
    required this.count,
    this.onClear,
  });

  @override
  Widget build(BuildContext context) {
    return Row(
      children: [
        Icon(icon, size: 16, color: color),
        const SizedBox(width: 8),
        Text(
          label,
          style: const TextStyle(
            fontSize: 14,
            fontWeight: FontWeight.w700,
            color: AppColors.onSurface,
          ),
        ),
        const Spacer(),
        if (onClear != null)
          TextButton(
            onPressed: onClear,
            style: TextButton.styleFrom(
              foregroundColor: AppColors.error,
              minimumSize: Size.zero,
              padding:
                  const EdgeInsets.symmetric(horizontal: 8, vertical: 4),
            ),
            child: const Text('Clear all',
                style: TextStyle(fontSize: 11)),
          )
        else
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

class _ShoppingItemTile extends StatelessWidget {
  final String name;
  final String? subtitle;
  final bool checked;
  final VoidCallback onTap;
  final VoidCallback? onDelete;
  final Color accentColor;

  const _ShoppingItemTile({
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
        padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 14),
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
                  color: checked ? AppColors.primary : AppColors.outline,
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
                icon: const Icon(Icons.close_rounded,
                    size: 16, color: AppColors.outline),
                constraints:
                    const BoxConstraints(minWidth: 32, minHeight: 32),
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
