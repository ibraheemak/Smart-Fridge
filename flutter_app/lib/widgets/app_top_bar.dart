import 'package:flutter/material.dart';
import '../services/auth_service.dart';
import '../screens/users_screen.dart';
import '../theme/app_theme.dart';

/// Shared floating app bar for the four main tabs. The avatar shows the
/// signed-in user's initial and opens the Account screen.
class AppTopBar extends StatelessWidget {
  const AppTopBar({super.key});

  @override
  Widget build(BuildContext context) {
    final name = AuthService.displayName;
    return SliverAppBar(
      floating: true,
      backgroundColor: AppColors.background,
      elevation: 0,
      scrolledUnderElevation: 0,
      titleSpacing: 16,
      title: Text('Smart Fridge',
          style: Theme.of(context).textTheme.headlineMedium),
      actions: [
        Padding(
          padding: const EdgeInsets.only(right: 16),
          child: GestureDetector(
            onTap: () => Navigator.push(context,
                MaterialPageRoute(builder: (_) => const UsersScreen())),
            child: CircleAvatar(
              radius: 18,
              backgroundColor: AppColors.primaryFixed,
              child: Text(
                name.isNotEmpty ? name[0].toUpperCase() : 'U',
                style: const TextStyle(
                  fontSize: 14,
                  fontWeight: FontWeight.w700,
                  color: AppColors.primary,
                ),
              ),
            ),
          ),
        ),
      ],
    );
  }
}
