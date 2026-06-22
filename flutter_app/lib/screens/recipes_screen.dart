import 'package:flutter/material.dart';
import '../services/fridge_service.dart';
import '../services/gemini_service.dart';
import '../theme/app_theme.dart';

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

  Future<void> _fetchRecipes() async {
    setState(() {
      _loading = true;
      _error = '';
    });
    try {
      final inv = await FridgeService.inventoryStream().first;
      if (inv == null || inv.items.isEmpty) {
        setState(() => _error = 'Fridge is empty — run a scan first.');
        return;
      }
      final result = await GeminiService.getRecipes(inv.items);
      setState(() => _recipes = result);
    } catch (e) {
      setState(() =>
          _error = e.toString().replaceFirst('Exception: ', ''));
    } finally {
      if (mounted) setState(() => _loading = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: AppColors.background,
      body: SafeArea(
        child: CustomScrollView(
          slivers: [
            // ── App Bar ────────────────────────────────────────────────────
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
                            onTap: () =>
                                setState(() => _filter = f),
                            child: AnimatedContainer(
                              duration:
                                  const Duration(milliseconds: 180),
                              padding: const EdgeInsets.symmetric(
                                  horizontal: 20, vertical: 6),
                              decoration: BoxDecoration(
                                color: active
                                    ? AppColors.secondaryContainer
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
                          decoration: BoxDecoration(
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
            else
              SliverPadding(
                padding: const EdgeInsets.fromLTRB(16, 0, 16, 100),
                sliver: SliverList(
                  delegate: SliverChildBuilderDelegate(
                    (_, i) {
                      if (i == 0) {
                        return Column(
                          children: [
                            // Featured card
                            _FeaturedRecipeCard(recipe: _recipes!.first),
                            const SizedBox(height: 16),
                            // Regenerate
                          ],
                        );
                      }
                      if (i <= _recipes!.length - 1) {
                        return Padding(
                          padding: const EdgeInsets.only(bottom: 12),
                          child: _RecipeCard(
                              recipe: _recipes![i]),
                        );
                      }
                      // Regenerate button at end
                      return Padding(
                        padding: const EdgeInsets.only(top: 8, bottom: 16),
                        child: Center(
                          child: OutlinedButton.icon(
                            onPressed: _fetchRecipes,
                            icon: const Icon(Icons.refresh_rounded,
                                color: AppColors.primary),
                            label: const Text('Regenerate',
                                style:
                                    TextStyle(color: AppColors.primary)),
                            style: OutlinedButton.styleFrom(
                              side: const BorderSide(
                                  color: AppColors.primary),
                              shape: RoundedRectangleBorder(
                                  borderRadius:
                                      BorderRadius.circular(12)),
                            ),
                          ),
                        ),
                      );
                    },
                    childCount: _recipes!.length + 1,
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
                Text(
                  recipe.time,
                  style: const TextStyle(
                      color: Colors.white70, fontSize: 13),
                ),
                const SizedBox(width: 16),
                const Icon(Icons.restaurant_rounded,
                    color: Colors.white70, size: 14),
                const SizedBox(width: 4),
                Text(
                  recipe.difficulty,
                  style: const TextStyle(
                      color: Colors.white70, fontSize: 13),
                ),
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
                Icon(Icons.timer_rounded,
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
                          fontSize: 11,
                          color: AppColors.onSurface),
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
