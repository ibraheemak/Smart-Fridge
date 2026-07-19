import 'package:flutter/material.dart';
import '../services/fridge_service.dart';
import '../services/gemini_service.dart';
import '../theme/app_theme.dart';
import '../widgets/app_top_bar.dart';

class RecipesScreen extends StatefulWidget {
  const RecipesScreen({super.key});

  @override
  State<RecipesScreen> createState() => _RecipesScreenState();
}

class _RecipesScreenState extends State<RecipesScreen> {
  List<RecipeSuggestion>? _recipes;
  bool _loading = false;
  String _error = '';
  String _filter = 'All';

  final _filters = ['All', 'Breakfast', 'Vegetarian', 'Quick (≤15 min)'];

  // Meat keywords for vegetarian filter
  static const _meatKeywords = [
    'chicken', 'beef', 'fish', 'lamb', 'pork', 'meat',
    'turkey', 'bacon', 'ham', 'tuna', 'shrimp', 'salmon', 'sausage',
  ];

  // Breakfast keywords
  static const _breakfastKeywords = [
    'breakfast', 'omelette', 'omelet', 'pancake', 'cereal',
    'toast', 'smoothie', 'granola', 'waffle', 'french toast', 'scrambled egg',
    'muffin', 'porridge', 'oatmeal',
  ];

  List<RecipeSuggestion> get _filtered {
    final all = _recipes ?? [];
    if (_filter == 'All') return all;
    if (_filter == 'Breakfast') {
      return all.where((r) {
        final lower = '${r.name} ${r.description}'.toLowerCase();
        return _breakfastKeywords.any((k) => lower.contains(k));
      }).toList();
    }
    if (_filter == 'Vegetarian') {
      return all.where((r) {
        final allIngredients = r.ingredients.join(' ').toLowerCase();
        return !_meatKeywords.any((m) => allIngredients.contains(m));
      }).toList();
    }
    if (_filter == 'Quick (≤15 min)') {
      return all.where((r) {
        if (r.time.toLowerCase().contains('hour')) return false;
        final m = RegExp(r'(\d+)').firstMatch(r.time);
        if (m == null) return false;
        return int.parse(m.group(1)!) <= 15;
      }).toList();
    }
    return all;
  }

  Future<void> _fetchRecipes() async {
    setState(() {
      _loading = true;
      _error = '';
    });
    try {
      final inv = await FridgeService.inventoryStream().first;
      if (!mounted) return;
      if (inv == null || inv.items.isEmpty) {
        setState(() => _error = 'Fridge is empty — run a scan first.');
        return;
      }
      final result = await GeminiService.getRecipes(inv.items);
      if (!mounted) return;
      setState(() => _recipes = result);
    } catch (e) {
      if (!mounted) return;
      setState(() =>
          _error = e.toString().replaceFirst('Exception: ', ''));
    } finally {
      if (mounted) setState(() => _loading = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    final filtered = _filtered;

    return Scaffold(
      backgroundColor: AppColors.background,
      body: SafeArea(
        child: CustomScrollView(
          slivers: [
            // ── App Bar ────────────────────────────────────────────────────
            const AppTopBar(),

            SliverToBoxAdapter(
              child: Padding(
                padding: const EdgeInsets.fromLTRB(16, 8, 16, 0),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      'What to cook today?',
                      style: Theme.of(context)
                          .textTheme
                          .titleLarge
                          ?.copyWith(fontWeight: FontWeight.w700),
                    ),
                    const SizedBox(height: 4),
                    Text(
                      'Based on what\'s in your fridge',
                      style: Theme.of(context).textTheme.bodyMedium,
                    ),
                    const SizedBox(height: 16),

                    // Filter chips
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
                            onTap: () => setState(() => _filter = f),
                            child: AnimatedContainer(
                              duration: const Duration(milliseconds: 180),
                              padding: const EdgeInsets.symmetric(
                                  horizontal: 20, vertical: 6),
                              decoration: BoxDecoration(
                                color: active
                                    ? AppColors.secondaryContainer
                                    : AppColors.surfaceContainerHigh,
                                borderRadius: BorderRadius.circular(20),
                              ),
                              child: Text(
                                f,
                                style: TextStyle(
                                  fontSize: 12,
                                  fontWeight: FontWeight.w600,
                                  color: active
                                      ? AppColors.onSecondaryContainer
                                      : AppColors.onSurfaceVariant,
                                ),
                              ),
                            ),
                          );
                        },
                      ),
                    ),
                    const SizedBox(height: 20),
                  ],
                ),
              ),
            ),

            // ── Body ───────────────────────────────────────────────────────
            if (_loading)
              const SliverFillRemaining(
                child: Center(
                  child: Column(
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      CircularProgressIndicator(color: AppColors.primary),
                      SizedBox(height: 16),
                      Text('Asking Gemini AI…',
                          style: TextStyle(color: AppColors.onSurfaceVariant)),
                    ],
                  ),
                ),
              )
            else if (_recipes == null)
              SliverFillRemaining(
                child: Center(
                  child: Padding(
                    padding: const EdgeInsets.all(32),
                    child: Column(
                      mainAxisSize: MainAxisSize.min,
                      children: [
                        Container(
                          width: 80,
                          height: 80,
                          decoration: const BoxDecoration(
                            color: AppColors.primaryFixed,
                            shape: BoxShape.circle,
                          ),
                          child: const Icon(Icons.auto_awesome_rounded,
                              size: 40, color: AppColors.primary),
                        ),
                        const SizedBox(height: 20),
                        Text('AI Recipe Suggestions',
                            style:
                                Theme.of(context).textTheme.titleLarge),
                        const SizedBox(height: 8),
                        Text(
                          'Gemini AI suggests recipes based on\nwhat\'s currently in your fridge.',
                          textAlign: TextAlign.center,
                          style: Theme.of(context).textTheme.bodyMedium,
                        ),
                        if (_error.isNotEmpty) ...[
                          const SizedBox(height: 12),
                          Container(
                            padding: const EdgeInsets.all(12),
                            decoration: BoxDecoration(
                              color: AppColors.errorContainer,
                              borderRadius: BorderRadius.circular(12),
                            ),
                            child: Text(
                              _error,
                              textAlign: TextAlign.center,
                              style: const TextStyle(
                                  fontSize: 12,
                                  color: AppColors.onErrorContainer),
                            ),
                          ),
                        ],
                        const SizedBox(height: 24),
                        ElevatedButton.icon(
                          onPressed: _fetchRecipes,
                          icon: const Icon(Icons.auto_awesome_rounded),
                          label: const Text('Generate Recipes'),
                        ),
                      ],
                    ),
                  ),
                ),
              )
            else if (filtered.isEmpty)
              SliverFillRemaining(
                child: Center(
                  child: Padding(
                    padding: const EdgeInsets.all(32),
                    child: Column(
                      mainAxisSize: MainAxisSize.min,
                      children: [
                        const Icon(Icons.filter_list_rounded,
                            size: 56, color: AppColors.outline),
                        const SizedBox(height: 16),
                        Text('No matches for "$_filter"',
                            style: Theme.of(context).textTheme.titleLarge),
                        const SizedBox(height: 8),
                        const Text(
                            'Try a different filter or regenerate recipes.',
                            textAlign: TextAlign.center,
                            style: TextStyle(
                                color: AppColors.onSurfaceVariant)),
                        const SizedBox(height: 20),
                        TextButton(
                          onPressed: () => setState(() => _filter = 'All'),
                          child: const Text('Clear filter'),
                        ),
                      ],
                    ),
                  ),
                ),
              )
            else
              SliverPadding(
                padding: const EdgeInsets.fromLTRB(16, 0, 16, 100),
                sliver: SliverList(
                  delegate: SliverChildBuilderDelegate(
                    (_, i) {
                      if (i == 0) {
                        return Column(
                          children: [
                            _FeaturedRecipeCard(recipe: filtered.first),
                            const SizedBox(height: 16),
                          ],
                        );
                      }
                      if (i <= filtered.length - 1) {
                        return Padding(
                          padding: const EdgeInsets.only(bottom: 12),
                          child: _RecipeCard(recipe: filtered[i]),
                        );
                      }
                      // Regenerate at end
                      return Padding(
                        padding: const EdgeInsets.only(top: 8, bottom: 16),
                        child: Center(
                          child: OutlinedButton.icon(
                            onPressed: _fetchRecipes,
                            icon: const Icon(Icons.refresh_rounded,
                                color: AppColors.primary),
                            label: const Text('Regenerate',
                                style: TextStyle(color: AppColors.primary)),
                            style: OutlinedButton.styleFrom(
                              side: const BorderSide(color: AppColors.primary),
                              shape: RoundedRectangleBorder(
                                  borderRadius: BorderRadius.circular(12)),
                            ),
                          ),
                        ),
                      );
                    },
                    childCount: filtered.length + 1,
                  ),
                ),
              ),
          ],
        ),
      ),
    );
  }
}

class _FeaturedRecipeCard extends StatelessWidget {
  final RecipeSuggestion recipe;

  const _FeaturedRecipeCard({required this.recipe});

  @override
  Widget build(BuildContext context) {
    return Container(
      height: 200,
      decoration: BoxDecoration(
        borderRadius: BorderRadius.circular(20),
        gradient: const LinearGradient(
          begin: Alignment.topLeft,
          end: Alignment.bottomRight,
          colors: [Color(0xFF1A237E), Color(0xFF3949AB)],
        ),
        boxShadow: const [
          BoxShadow(
              color: Color(0x291A237E),
              blurRadius: 20,
              offset: Offset(0, 6)),
        ],
      ),
      child: Padding(
        padding: const EdgeInsets.all(20),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Container(
              padding:
                  const EdgeInsets.symmetric(horizontal: 10, vertical: 4),
              decoration: BoxDecoration(
                color: AppColors.tertiaryFixed,
                borderRadius: BorderRadius.circular(20),
              ),
              child: const Text(
                'AI RECOMMENDED',
                style: TextStyle(
                  color: AppColors.onTertiaryFixed,
                  fontSize: 10,
                  fontWeight: FontWeight.w800,
                  letterSpacing: 0.5,
                ),
              ),
            ),
            const Spacer(),
            Text(
              recipe.name,
              style: const TextStyle(
                color: Colors.white,
                fontSize: 22,
                fontWeight: FontWeight.w700,
              ),
              maxLines: 2,
              overflow: TextOverflow.ellipsis,
            ),
            const SizedBox(height: 8),
            Row(
              children: [
                const Icon(Icons.schedule_rounded,
                    color: Colors.white70, size: 14),
                const SizedBox(width: 4),
                Text(recipe.time,
                    style: const TextStyle(color: Colors.white70, fontSize: 13)),
                const SizedBox(width: 16),
                const Icon(Icons.restaurant_rounded,
                    color: Colors.white70, size: 14),
                const SizedBox(width: 4),
                Text(recipe.difficulty,
                    style: const TextStyle(color: Colors.white70, fontSize: 13)),
              ],
            ),
          ],
        ),
      ),
    );
  }
}

class _RecipeCard extends StatelessWidget {
  final RecipeSuggestion recipe;

  const _RecipeCard({required this.recipe});

  Color get _diffColor {
    switch (recipe.difficulty.toLowerCase()) {
      case 'easy':
        return AppColors.secondary;
      case 'hard':
        return AppColors.error;
      default:
        return AppColors.tertiaryFixedDim;
    }
  }

  @override
  Widget build(BuildContext context) {
    return Container(
      decoration: BoxDecoration(
        color: AppColors.surfaceContainerLowest,
        borderRadius: BorderRadius.circular(16),
        border: Border.all(
            color: AppColors.outlineVariant.withValues(alpha: 0.4)),
        boxShadow: const [
          BoxShadow(
              color: Color(0x08000000),
              blurRadius: 8,
              offset: Offset(0, 2)),
        ],
      ),
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            // Title + difficulty badge
            Row(
              children: [
                Expanded(
                  child: Text(
                    recipe.name,
                    style: Theme.of(context)
                        .textTheme
                        .titleMedium
                        ?.copyWith(fontWeight: FontWeight.w700),
                  ),
                ),
                Container(
                  padding: const EdgeInsets.symmetric(
                      horizontal: 10, vertical: 4),
                  decoration: BoxDecoration(
                    color: _diffColor.withValues(alpha: 0.15),
                    borderRadius: BorderRadius.circular(20),
                  ),
                  child: Text(
                    recipe.difficulty,
                    style: TextStyle(
                      fontSize: 11,
                      fontWeight: FontWeight.w700,
                      color: _diffColor,
                    ),
                  ),
                ),
              ],
            ),
            const SizedBox(height: 6),

            // Time
            Row(
              children: [
                const Icon(Icons.timer_rounded,
                    size: 13, color: AppColors.onSurfaceVariant),
                const SizedBox(width: 4),
                Text(recipe.time,
                    style: Theme.of(context).textTheme.labelLarge),
              ],
            ),
            const SizedBox(height: 8),

            // Description
            Text(
              recipe.description,
              style: Theme.of(context).textTheme.bodyMedium,
              maxLines: 3,
              overflow: TextOverflow.ellipsis,
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
                        horizontal: 10, vertical: 4),
                    decoration: BoxDecoration(
                      color: AppColors.surfaceContainerLow,
                      borderRadius: BorderRadius.circular(20),
                    ),
                    child: Text(
                      ing,
                      style: const TextStyle(
                          fontSize: 11, color: AppColors.onSurface),
                    ),
                  );
                }).toList(),
              ),
            ],
          ],
        ),
      ),
    );
  }
}
