import 'dart:typed_data';
import 'package:cached_network_image/cached_network_image.dart';
import 'package:flutter/material.dart';
import '../services/fridge_service.dart';
import '../services/icon_generator_service.dart';
import '../theme/app_theme.dart';

/// Smart item icon widget:
/// 1. Loads icon from Firebase Storage (icons/{name}.jpg)
/// 2. If missing, calls Gemini AI to generate one
/// 3. Falls back to a styled letter avatar if AI also fails
class ItemIcon extends StatefulWidget {
  final String itemName;
  final double size;
  final BorderRadius? borderRadius;

  const ItemIcon({
    super.key,
    required this.itemName,
    this.size = 52,
    this.borderRadius,
  });

  @override
  State<ItemIcon> createState() => _ItemIconState();
}

class _ItemIconState extends State<ItemIcon> {
  Uint8List? _generated;
  bool _generating = false;
  bool _generationFailed = false;

  void _startGeneration() {
    if (_generating || _generationFailed || _generated != null) return;
    setState(() => _generating = true);
    IconGeneratorService.generateIcon(widget.itemName).then((bytes) {
      if (!mounted) return;
      setState(() {
        _generated = bytes;
        _generating = false;
        _generationFailed = bytes == null;
      });
    });
  }

  @override
  Widget build(BuildContext context) {
    final radius = widget.borderRadius ?? BorderRadius.circular(10);

    return ClipRRect(
      borderRadius: radius,
      child: SizedBox(
        width: widget.size,
        height: widget.size,
        child: _generated != null
            // ── AI-generated image ────────────────────────────────────────
            ? Image.memory(_generated!, fit: BoxFit.cover)
            : CachedNetworkImage(
                imageUrl: FridgeService.iconUrl(widget.itemName),
                fit: BoxFit.cover,
                fadeInDuration: const Duration(milliseconds: 200),
                placeholder: (_, __) => _Placeholder(
                  loading: true,
                  itemName: widget.itemName,
                  size: widget.size,
                ),
                errorWidget: (ctx, _, __) {
                  // Firebase Storage icon missing → trigger AI generation
                  WidgetsBinding.instance.addPostFrameCallback(
                      (_) => _startGeneration());

                  if (_generating) {
                    return _Placeholder(
                      loading: true,
                      itemName: widget.itemName,
                      size: widget.size,
                      label: 'AI…',
                    );
                  }
                  if (_generationFailed) {
                    // Final fallback: coloured letter avatar
                    return _LetterAvatar(
                        itemName: widget.itemName, size: widget.size);
                  }
                  // Still waiting for generation to start
                  return _Placeholder(
                    loading: false,
                    itemName: widget.itemName,
                    size: widget.size,
                  );
                },
              ),
      ),
    );
  }
}

/// Shows a shimmer-style loading box or a generic icon.
class _Placeholder extends StatelessWidget {
  final bool loading;
  final String itemName;
  final double size;
  final String? label;

  const _Placeholder(
      {required this.loading,
      required this.itemName,
      required this.size,
      this.label});

  @override
  Widget build(BuildContext context) {
    return Container(
      width: size,
      height: size,
      color: AppColors.surfaceContainerHigh,
      child: loading
          ? Center(
              child: Column(
                mainAxisSize: MainAxisSize.min,
                children: [
                  SizedBox(
                    width: size * 0.3,
                    height: size * 0.3,
                    child: const CircularProgressIndicator(
                        strokeWidth: 2, color: AppColors.primary),
                  ),
                  if (label != null) ...[
                    const SizedBox(height: 4),
                    Text(label!,
                        style: TextStyle(
                            fontSize: size * 0.15,
                            color: AppColors.primary,
                            fontWeight: FontWeight.w700)),
                  ]
                ],
              ),
            )
          : const Icon(Icons.fastfood_rounded, color: AppColors.outline),
    );
  }
}

/// Colored circle with item initial — used as last-resort fallback.
class _LetterAvatar extends StatelessWidget {
  final String itemName;
  final double size;

  const _LetterAvatar({required this.itemName, required this.size});

  static const _colors = [
    Color(0xFF1A237E),
    Color(0xFF006E1C),
    Color(0xFF5B4300),
    Color(0xFF7B1FA2),
    Color(0xFF00695C),
    Color(0xFFBF360C),
    Color(0xFF1565C0),
    Color(0xFF2E7D32),
  ];

  Color get _bg {
    final idx = itemName.isNotEmpty ? itemName.codeUnitAt(0) % _colors.length : 0;
    return _colors[idx];
  }

  @override
  Widget build(BuildContext context) {
    final letter =
        itemName.isNotEmpty ? itemName[0].toUpperCase() : '?';
    return Container(
      width: size,
      height: size,
      color: _bg.withValues(alpha: 0.15),
      child: Center(
        child: Text(
          letter,
          style: TextStyle(
            fontSize: size * 0.4,
            fontWeight: FontWeight.w800,
            color: _bg,
          ),
        ),
      ),
    );
  }
}
