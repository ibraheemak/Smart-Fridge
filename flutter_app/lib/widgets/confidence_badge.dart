import 'package:flutter/material.dart';
import '../theme/app_theme.dart';

/// 10 px colored dot: green = high confidence, amber = medium, red = low.
class ConfidenceBadge extends StatelessWidget {
  final String confidence;

  const ConfidenceBadge({super.key, required this.confidence});

  Color get _color {
    switch (confidence) {
      case 'high':
        return AppColors.success;
      case 'medium':
        return AppColors.warning;
      default:
        return AppColors.error;
    }
  }

  @override
  Widget build(BuildContext context) {
    return Container(
      width: 10,
      height: 10,
      decoration: BoxDecoration(color: _color, shape: BoxShape.circle),
    );
  }
}
