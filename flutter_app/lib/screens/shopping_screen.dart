import 'package:flutter/material.dart';
import '../models/fridge_item.dart';
import '../services/fridge_service.dart';
import '../services/gemini_service.dart';
import '../theme/app_theme.dart';

// Common fridge staples — items absent from inventory appear in the shopping list
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

class _ShoppingScreenState extends State<ShoppingScreen>
    with SingleTickerProviderStateMixin {
  late final TabController _tab;
  List<RecipeSuggestion>? _recipes;
  bool _loadingRecipes = false;
  String _recipeError = '';
  final Set<String> _checked = {};

  @override
  void initState() {
    super.initState();
    _tab = TabController(length: 2, vsync: this);
  }

  @override
  void dispose() {
    _tab.dispose();
    super.dispose();
  }

  Future<void> _fetchRecipes() async {
    setState(() {
      _loadingRecipes = true;
      _recipeError = '';
    });
    try {
      final inv = await FridgeService.inventoryStream().first;
      if (inv == null || inv.items.isEmpty) {
        setState(
            () => _recipeError = 'Fridge is empty — run a scan first.');
        return;
      }
      final result = await GeminiService.getRecipes(inv.items);
      setState(() => _recipes = result);
    } catch (e) {
      setState(() =>
          _recipeError = e.toString().replaceFirst('Exception: ', ''));
    } finally {
      setState(() => _loadingRecipes = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: SafeArea(
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Padding(
              padding: const EdgeInsets.fromLTRB(20, 24, 20, 12),
              child: Text('Shopping & Recipes',
                  style: Theme.of(context).textTheme.headlineMedium),
            ),

            // Pill tab bar
            Padding(
              padding: const EdgeInsets.symmetric(horizontal: 16),
              child: Container(
                height: 44,
                padding: const EdgeInsets.all(4),
                decoration: BoxDecoration(
                  color: AppColors.divider,
                  borderRadius: BorderRadius.circular(12),
                ),
                child: TabBar(
                  controller: _tab,
                  indicator: BoxDecoration(
                    color: AppColors.card,
                    borderRadius: BorderRadius.circular(8),
                    boxShadow: const [
                      BoxShadow(
                          color: AppColors.shadow,
                          blurRadius: 4,
                          offset: Offset(0, 1))
                    ],
                  ),
                  indicatorSize: TabBarIndicatorSize.tab,
                  dividerColor: Colors.transparent,
                  labelColor: AppColors.primary,
                  unselectedLabelColor: AppColors.textSecondary,
                  labelStyle: const TextStyle(
                      fontSize: 13, fontWeight: FontWeight.w600),
                  tabs: const [
                    Tab(text: '🛒  Shopping List'),
                    Tab(text: '👨‍🍳  Recipes'),
                  ],
                ),
              ),
            ),
            const SizedBox(height: 8),

            Expanded(
              child: TabBarView(
                controller: _tab,
                children: [
                  _ShoppingListTab(
                    checked: _checked,
                    onToggle: (name) => setState(() => _checked.contains(name)
                        ? _checked.remove(name)
                        : _checked.add(name)),
                  ),
                  _RecipesTab(
                    recipes: _recipes,
                    loading: _loadingRecipes,
                    error: _recipeError,
                    onGenerate: _fetchRecipes,
                  ),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }
}

// ── Shopping list ──────────────────────────────────────────────────────────
class _ShoppingListTab extends StatelessWidget {
  final Set<String> checked;
  final ValueChanged<String> onToggle;

  const _ShoppingListTab({required this.checked, required this.onToggle});

  @override
  Widget build(BuildContext context) {
    return StreamBuilder<InventorySnapshot?>(
      stream: FridgeService.inventoryStream(),
      builder: (_, snap) {
        final inFridge =
            snap.data?.items.map((i) => i.name.toLowerCase()).toSet() ?? {};
        final missing =
            _staples.where((s) => !inFridge.contains(s)).toList();
        final lowConf = snap.data?.items
                .where((i) => i.confidence == 'low')
                .map((i) => i.name)
                .toList() ??
            [];

        if (missing.isEmpty && lowConf.isEmpty) {
          return const Center(
            child: Column(
              mainAxisSize: MainAxisSize.min,
              children: [
                Text('🎉', style: TextStyle(fontSize: 72)),
                SizedBox(height: 16),
                Text('All stocked up!',
                    style: TextStyle(
                        fontSize: 18,
                        fontWeight: FontWeight.w700,
                        color: AppColors.text)),
                SizedBox(height: 8),
                Text('Nothing missing from the fridge.',
                    style: TextStyle(color: AppColors.textSecondary)),
              ],
            ),
          );
        }

        return ListView(
          padding: const EdgeInsets.fromLTRB(16, 8, 16, 100),
          children: [
            if (lowConf.isNotEmpty) ...[
              _SectionLabel(
                  icon: Icons.warning_amber_rounded,
                  color: AppColors.warning,
                  label: 'Possibly needed (low confidence)'),
              const SizedBox(height: 8),
              ...lowConf.map((n) => _ListItem(
                    name: n,
                    subtitle: 'Detected but uncertain',
                    checked: checked.contains(n),
                    onToggle: () => onToggle(n),
                  )),
              const SizedBox(height: 16),
            ],
            if (missing.isNotEmpty) ...[
              _SectionLabel(
                  icon: Icons.remove_shopping_cart_rounded,
                  color: AppColors.primary,
                  label: 'Not in fridge'),
              const SizedBox(height: 8),
              ...missing.map((n) => _ListItem(
                    name: _cap(n),
                    checked: checked.contains(n),
                    onToggle: () => onToggle(n),
                  )),
            ],
          ],
        );
      },
    );
  }

  static String _cap(String s) =>
      s.isEmpty ? s : s[0].toUpperCase() + s.substring(1);
}

class _SectionLabel extends StatelessWidget {
  final IconData icon;
  final Color color;
  final String label;
  const _SectionLabel(
      {required this.icon, required this.color, required this.label});

  @override
  Widget build(BuildContext context) {
    return Row(
      children: [
        Icon(icon, size: 15, color: color),
        const SizedBox(width: 6),
        Text(label,
            style: TextStyle(
                fontSize: 12, fontWeight: FontWeight.w600, color: color)),
      ],
    );
  }
}

class _ListItem extends StatelessWidget {
  final String name;
  final String? subtitle;
  final bool checked;
  final VoidCallback onToggle;

  const _ListItem({
    required this.name,
    this.subtitle,
    required this.checked,
    required this.onToggle,
  });

  @override
  Widget build(BuildContext context) {
    return GestureDetector(
      onTap: onToggle,
      child: Container(
        margin: const EdgeInsets.only(bottom: 8),
        padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 12),
        decoration: BoxDecoration(
          color: AppColors.card,
          borderRadius: BorderRadius.circular(12),
          boxShadow: const [
            BoxShadow(
                color: AppColors.shadow,
                blurRadius: 6,
                offset: Offset(0, 2))
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
                color:
                    checked ? AppColors.primary : Colors.transparent,
                border: Border.all(
                  color:
                      checked ? AppColors.primary : AppColors.divider,
                  width: 2,
                ),
                borderRadius: BorderRadius.circular(6),
              ),
              child: checked
                  ? const Icon(Icons.check_rounded,
                      size: 14, color: Colors.white)
                  : null,
            ),
            const SizedBox(width: 12),
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    name,
                    style: TextStyle(
                      fontSize: 14,
                      fontWeight: FontWeight.w500,
                      color: checked
                          ? AppColors.textHint
                          : AppColors.text,
                      decoration: checked
                          ? TextDecoration.lineThrough
                          : null,
                    ),
                  ),
                  if (subtitle != null)
                    Text(subtitle!,
                        style: const TextStyle(
                            fontSize: 11,
                            color: AppColors.textSecondary)),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }
}

// ── Recipes tab ────────────────────────────────────────────────────────────
class _RecipesTab extends StatelessWidget {
  final List<RecipeSuggestion>? recipes;
  final bool loading;
  final String error;
  final VoidCallback onGenerate;

  const _RecipesTab({
    required this.recipes,
    required this.loading,
    required this.error,
    required this.onGenerate,
  });

  @override
  Widget build(BuildContext context) {
    if (loading) {
      return const Center(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            CircularProgressIndicator(color: AppColors.primary),
            SizedBox(height: 16),
            Text('Asking Gemini AI…',
                style: TextStyle(color: AppColors.textSecondary)),
          ],
        ),
      );
    }

    if (recipes == null) {
      return Center(
        child: Padding(
          padding: const EdgeInsets.all(32),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              const Text('🤖', style: TextStyle(fontSize: 72)),
              const SizedBox(height: 16),
              Text('AI Recipe Suggestions',
                  style: Theme.of(context).textTheme.titleLarge),
              const SizedBox(height: 8),
              const Text(
                'Gemini AI suggests recipes based on\nwhat\'s currently in your fridge.',
                textAlign: TextAlign.center,
                style: TextStyle(
                    color: AppColors.textSecondary, height: 1.5),
              ),
              if (error.isNotEmpty) ...[
                const SizedBox(height: 12),
                Container(
                  padding: const EdgeInsets.all(12),
                  decoration: BoxDecoration(
                    color: AppColors.error.withOpacity(0.08),
                    borderRadius: BorderRadius.circular(10),
                    border: Border.all(
                        color: AppColors.error.withOpacity(0.3)),
                  ),
                  child: Text(error,
                      textAlign: TextAlign.center,
                      style: const TextStyle(
                          fontSize: 12, color: AppColors.error)),
                ),
              ],
              const SizedBox(height: 24),
              FilledButton.icon(
                onPressed: onGenerate,
                icon: const Icon(Icons.auto_awesome_rounded),
                label: const Text('Generate Recipes'),
                style: FilledButton.styleFrom(
                  backgroundColor: AppColors.primary,
                  padding: const EdgeInsets.symmetric(
                      horizontal: 24, vertical: 14),
                  shape: RoundedRectangleBorder(
                      borderRadius: BorderRadius.circular(14)),
                ),
              ),
            ],
          ),
        ),
      );
    }

    return ListView(
      padding: const EdgeInsets.fromLTRB(16, 8, 16, 100),
      children: [
        ...recipes!.map(
          (r) => Padding(
            padding: const EdgeInsets.only(bottom: 12),
            child: _RecipeCard(recipe: r),
          ),
        ),
        Center(
          child: TextButton.icon(
            onPressed: onGenerate,
            icon: const Icon(Icons.refresh_rounded,
                color: AppColors.primary),
            label: const Text('Regenerate',
                style: TextStyle(color: AppColors.primary)),
          ),
        ),
      ],
    );
  }
}

class _RecipeCard extends StatelessWidget {
  final RecipeSuggestion recipe;
  const _RecipeCard({required this.recipe});

  Color get _diffColor {
    switch (recipe.difficulty.toLowerCase()) {
      case 'easy':
        return AppColors.success;
      case 'hard':
        return AppColors.error;
      default:
        return AppColors.warning;
    }
  }

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: AppColors.card,
        borderRadius: BorderRadius.circular(16),
        boxShadow: const [
          BoxShadow(
              color: AppColors.shadow,
              blurRadius: 10,
              offset: Offset(0, 3))
        ],
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          // Title + difficulty
          Row(
            children: [
              Expanded(
                child: Text(
                  recipe.name,
                  style: const TextStyle(
                      fontSize: 16,
                      fontWeight: FontWeight.w700,
                      color: AppColors.text),
                ),
              ),
              Container(
                padding: const EdgeInsets.symmetric(
                    horizontal: 8, vertical: 3),
                decoration: BoxDecoration(
                  color: _diffColor.withOpacity(0.1),
                  borderRadius: BorderRadius.circular(8),
                ),
                child: Text(
                  recipe.difficulty,
                  style: TextStyle(
                      fontSize: 11,
                      fontWeight: FontWeight.w600,
                      color: _diffColor),
                ),
              ),
            ],
          ),
          const SizedBox(height: 4),

          // Time
          Row(
            children: [
              const Icon(Icons.timer_rounded,
                  size: 13, color: AppColors.textSecondary),
              const SizedBox(width: 4),
              Text(recipe.time,
                  style: const TextStyle(
                      fontSize: 12, color: AppColors.textSecondary)),
            ],
          ),
          const SizedBox(height: 8),

          // Description
          Text(
            recipe.description,
            style: const TextStyle(
                fontSize: 13,
                color: AppColors.textSecondary,
                height: 1.4),
          ),

          // Ingredient chips
          if (recipe.ingredients.isNotEmpty) ...[
            const SizedBox(height: 10),
            Wrap(
              spacing: 6,
              runSpacing: 4,
              children: recipe.ingredients.map((ing) {
                return Container(
                  padding: const EdgeInsets.symmetric(
                      horizontal: 8, vertical: 3),
                  decoration: BoxDecoration(
                    color: AppColors.background,
                    borderRadius: BorderRadius.circular(8),
                    border: Border.all(color: AppColors.divider),
                  ),
                  child: Text(ing,
                      style: const TextStyle(
                          fontSize: 11, color: AppColors.text)),
                );
              }).toList(),
            ),
          ],
        ],
      ),
    );
  }
}
