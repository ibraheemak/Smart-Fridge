import 'package:flutter/material.dart';
import '../services/auth_service.dart';
import '../theme/app_theme.dart';

class UsersScreen extends StatefulWidget {
  const UsersScreen({super.key});

  @override
  State<UsersScreen> createState() => _UsersScreenState();
}

class _UsersScreenState extends State<UsersScreen> {
  final _searchCtrl = TextEditingController();
  String _search = '';

  final _users = [
    _UserModel(
        name: 'Admin (You)',
        email: '',
        role: 'Admin',
        lastActive: 'Just now',
        isYou: true),
    _UserModel(
        name: 'Family Member 1',
        email: 'family1@email.com',
        role: 'Member',
        lastActive: '5h ago',
        isYou: false),
    _UserModel(
        name: 'Family Member 2',
        email: 'family2@email.com',
        role: 'Member',
        lastActive: 'Yesterday',
        isYou: false),
  ];

  List<_UserModel> get _filtered {
    if (_search.isEmpty) return _users;
    return _users
        .where((u) =>
            u.name.toLowerCase().contains(_search.toLowerCase()) ||
            u.email.toLowerCase().contains(_search.toLowerCase()))
        .toList();
  }

  @override
  void initState() {
    super.initState();
    // Fill admin email from Firebase Auth
    final email = AuthService.currentUser?.email ?? 'admin@smartfridge.app';
    _users[0] = _UserModel(
      name: AuthService.displayName,
      email: email,
      role: 'Admin',
      lastActive: 'Just now',
      isYou: true,
    );
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: AppColors.background,
      appBar: AppBar(
        backgroundColor: AppColors.background,
        elevation: 0,
        scrolledUnderElevation: 0,
        leading: IconButton(
          icon: const Icon(Icons.arrow_back_rounded, color: AppColors.onSurface),
          onPressed: () => Navigator.pop(context),
        ),
        title: const Text('User Management',
            style: TextStyle(
                color: AppColors.onSurface,
                fontSize: 20,
                fontWeight: FontWeight.w700)),
      ),
      floatingActionButton: FloatingActionButton(
        onPressed: _showAddUserDialog,
        backgroundColor: AppColors.primary,
        child: const Icon(Icons.person_add_rounded, color: Colors.white),
      ),
      body: Padding(
        padding: const EdgeInsets.fromLTRB(16, 8, 16, 16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            // Description
            Container(
              padding: const EdgeInsets.all(12),
              decoration: BoxDecoration(
                color: AppColors.primaryFixed,
                borderRadius: BorderRadius.circular(12),
              ),
              child: const Text(
                'Manage family members and their access to the Smart Fridge.',
                style: TextStyle(fontSize: 13, color: AppColors.primary),
              ),
            ),
            const SizedBox(height: 16),

            // Search
            TextField(
              controller: _searchCtrl,
              onChanged: (v) => setState(() => _search = v),
              decoration: InputDecoration(
                hintText: 'Search user...',
                prefixIcon: const Icon(Icons.search_rounded,
                    color: AppColors.onSurfaceVariant),
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
            const SizedBox(height: 16),

            // User list
            Expanded(
              child: ListView.separated(
                itemCount: _filtered.length,
                separatorBuilder: (_, __) => const SizedBox(height: 12),
                itemBuilder: (_, i) => _UserCard(
                  user: _filtered[i],
                  onDelete: _filtered[i].isYou
                      ? null
                      : () => _confirmDelete(_filtered[i]),
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }

  void _showAddUserDialog() {
    final nameCtrl = TextEditingController();
    final emailCtrl = TextEditingController();
    showDialog(
      context: context,
      builder: (_) => AlertDialog(
        title: const Text('Add Family Member'),
        content: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            TextField(
              controller: nameCtrl,
              decoration: InputDecoration(
                labelText: 'Name',
                border: OutlineInputBorder(
                    borderRadius: BorderRadius.circular(12)),
              ),
            ),
            const SizedBox(height: 12),
            TextField(
              controller: emailCtrl,
              keyboardType: TextInputType.emailAddress,
              decoration: InputDecoration(
                labelText: 'Email',
                border: OutlineInputBorder(
                    borderRadius: BorderRadius.circular(12)),
              ),
            ),
          ],
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(context),
            child: const Text('Cancel'),
          ),
          FilledButton(
            onPressed: () {
              if (nameCtrl.text.trim().isNotEmpty) {
                setState(() {
                  _users.add(_UserModel(
                    name: nameCtrl.text.trim(),
                    email: emailCtrl.text.trim(),
                    role: 'Member',
                    lastActive: 'Just invited',
                    isYou: false,
                  ));
                });
                Navigator.pop(context);
              }
            },
            child: const Text('Add'),
          ),
        ],
      ),
    );
  }

  void _confirmDelete(_UserModel user) {
    showDialog(
      context: context,
      builder: (_) => AlertDialog(
        title: const Text('Remove User'),
        content: Text('Remove ${user.name} from the Smart Fridge?'),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(context),
            child: const Text('Cancel'),
          ),
          TextButton(
            onPressed: () {
              setState(() => _users.remove(user));
              Navigator.pop(context);
            },
            style: TextButton.styleFrom(foregroundColor: AppColors.error),
            child: const Text('Remove'),
          ),
        ],
      ),
    );
  }

  @override
  void dispose() {
    _searchCtrl.dispose();
    super.dispose();
  }
}

class _UserModel {
  final String name;
  final String email;
  final String role;
  final String lastActive;
  final bool isYou;

  _UserModel({
    required this.name,
    required this.email,
    required this.role,
    required this.lastActive,
    required this.isYou,
  });
}

class _UserCard extends StatelessWidget {
  final _UserModel user;
  final VoidCallback? onDelete;

  const _UserCard({required this.user, this.onDelete});

  @override
  Widget build(BuildContext context) {
    final isAdmin = user.role == 'Admin';

    return Container(
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: AppColors.surfaceContainerLowest,
        borderRadius: BorderRadius.circular(16),
        border: isAdmin
            ? Border.all(color: AppColors.primary.withValues(alpha: 0.3))
            : Border.all(color: AppColors.outlineVariant.withValues(alpha: 0.4)),
        boxShadow: const [
          BoxShadow(
              color: Color(0x08000000),
              blurRadius: 6,
              offset: Offset(0, 2)),
        ],
      ),
      child: Row(
        children: [
          // Avatar
          CircleAvatar(
            radius: 26,
            backgroundColor:
                isAdmin ? AppColors.primaryFixed : AppColors.surfaceContainerHigh,
            child: Text(
              user.name.isNotEmpty ? user.name[0].toUpperCase() : 'U',
              style: TextStyle(
                fontSize: 20,
                fontWeight: FontWeight.w700,
                color:
                    isAdmin ? AppColors.primary : AppColors.onSurfaceVariant,
              ),
            ),
          ),
          const SizedBox(width: 14),

          // Info
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Row(
                  children: [
                    Flexible(
                      child: Text(user.name,
                          style: const TextStyle(
                              fontSize: 15, fontWeight: FontWeight.w700),
                          overflow: TextOverflow.ellipsis),
                    ),
                    const SizedBox(width: 8),
                    Container(
                      padding: const EdgeInsets.symmetric(
                          horizontal: 8, vertical: 2),
                      decoration: BoxDecoration(
                        color: isAdmin
                            ? AppColors.primaryFixed
                            : AppColors.surfaceContainerHigh,
                        borderRadius: BorderRadius.circular(20),
                      ),
                      child: Text(
                        user.role,
                        style: TextStyle(
                          fontSize: 10,
                          fontWeight: FontWeight.w700,
                          color: isAdmin
                              ? AppColors.primary
                              : AppColors.onSurfaceVariant,
                        ),
                      ),
                    ),
                  ],
                ),
                if (user.email.isNotEmpty)
                  Text(user.email,
                      style: const TextStyle(
                          fontSize: 12, color: AppColors.onSurfaceVariant),
                      overflow: TextOverflow.ellipsis),
                Text('Last active: ${user.lastActive}',
                    style: const TextStyle(
                        fontSize: 11, color: AppColors.outline)),
              ],
            ),
          ),

          // Actions
          if (!user.isYou) ...[
            IconButton(
              onPressed: () {},
              icon: const Icon(Icons.edit_rounded,
                  size: 18, color: AppColors.primary),
            ),
            IconButton(
              onPressed: onDelete,
              icon: const Icon(Icons.delete_rounded,
                  size: 18, color: AppColors.error),
            ),
          ],
        ],
      ),
    );
  }
}
