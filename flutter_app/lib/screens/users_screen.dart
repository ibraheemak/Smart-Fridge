import 'package:flutter/material.dart';
import '../services/auth_service.dart';
import '../services/fridge_session.dart';
import '../theme/app_theme.dart';

class UsersScreen extends StatelessWidget {
  const UsersScreen({super.key});

  @override
  Widget build(BuildContext context) {
    final user = AuthService.currentUser;
    final displayName = AuthService.displayName;
    final email = user?.email ?? '';
    final isAdmin = FridgeSession.isAdmin;
    final fridgeId = FridgeSession.fridgeId ?? '';

    return Scaffold(
      backgroundColor: AppColors.background,
      appBar: AppBar(
        backgroundColor: AppColors.background,
        elevation: 0,
        scrolledUnderElevation: 0,
        leading: IconButton(
          icon: const Icon(Icons.arrow_back_rounded,
              color: AppColors.onSurface),
          onPressed: () => Navigator.pop(context),
        ),
        title: const Text('Account',
            style: TextStyle(
                color: AppColors.onSurface,
                fontSize: 20,
                fontWeight: FontWeight.w700)),
      ),
      body: SingleChildScrollView(
        padding: const EdgeInsets.fromLTRB(16, 8, 16, 32),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            // ── Profile card ────────────────────────────────────────────
            Container(
              width: double.infinity,
              padding: const EdgeInsets.all(20),
              decoration: BoxDecoration(
                color: AppColors.surfaceContainerLowest,
                borderRadius: BorderRadius.circular(20),
                border: Border.all(
                    color: AppColors.primary.withValues(alpha: 0.3)),
                boxShadow: const [
                  BoxShadow(
                      color: Color(0x0A000000),
                      blurRadius: 8,
                      offset: Offset(0, 2)),
                ],
              ),
              child: Column(
                children: [
                  CircleAvatar(
                    radius: 36,
                    backgroundColor: AppColors.primaryFixed,
                    child: Text(
                      displayName.isNotEmpty
                          ? displayName[0].toUpperCase()
                          : 'U',
                      style: const TextStyle(
                        fontSize: 32,
                        fontWeight: FontWeight.w700,
                        color: AppColors.primary,
                      ),
                    ),
                  ),
                  const SizedBox(height: 16),
                  Text(
                    displayName,
                    style: const TextStyle(
                        fontSize: 20,
                        fontWeight: FontWeight.w700,
                        color: AppColors.onSurface),
                  ),
                  if (email.isNotEmpty) ...[
                    const SizedBox(height: 4),
                    Text(
                      email,
                      style: const TextStyle(
                          fontSize: 13,
                          color: AppColors.onSurfaceVariant),
                    ),
                  ],
                  const SizedBox(height: 12),
                  _RoleChip(isAdmin: isAdmin),
                ],
              ),
            ),
            const SizedBox(height: 24),

            // ── Member management (admin only) ───────────────────────────
            // ── Fridge ID (share with family so they can join) ───────────
            if (fridgeId.isNotEmpty) ...[
              _FridgeIdCard(fridgeId: fridgeId),
              const SizedBox(height: 24),
            ],

            if (isAdmin) ...[
              Text(
                'HOUSEHOLD MEMBERS',
                style: Theme.of(context).textTheme.labelLarge?.copyWith(
                      color: AppColors.onSurfaceVariant,
                      letterSpacing: 0.5,
                    ),
              ),
              const SizedBox(height: 4),
              const Text(
                'Approve people who requested access, and manage who is a manager.',
                style: TextStyle(
                    fontSize: 12, color: AppColors.onSurfaceVariant),
              ),
              const SizedBox(height: 12),
              _MembersList(currentUid: user?.uid ?? ''),
              const SizedBox(height: 24),
            ],

            // ── Actions ──────────────────────────────────────────────────
            Text(
              'ACCOUNT ACTIONS',
              style: Theme.of(context).textTheme.labelLarge?.copyWith(
                    color: AppColors.onSurfaceVariant,
                    letterSpacing: 0.5,
                  ),
            ),
            const SizedBox(height: 12),

            _ActionTile(
              icon: Icons.lock_reset_rounded,
              label: 'Change Password',
              subtitle: 'Send a password reset email',
              onTap: () async {
                if (email.isNotEmpty) {
                  try {
                    await AuthService.resetPassword(email);
                    if (context.mounted) {
                      ScaffoldMessenger.of(context).showSnackBar(SnackBar(
                        content:
                            Text('Password reset email sent to $email'),
                        backgroundColor: AppColors.secondary,
                      ));
                    }
                  } catch (e) {
                    if (context.mounted) {
                      ScaffoldMessenger.of(context).showSnackBar(SnackBar(
                        content: Text(e.toString()),
                        backgroundColor: AppColors.error,
                      ));
                    }
                  }
                }
              },
            ),

            const SizedBox(height: 8),

            _ActionTile(
              icon: Icons.logout_rounded,
              label: 'Sign Out',
              subtitle: 'Sign out of your account',
              color: AppColors.error,
              onTap: () async {
                final confirmed = await showDialog<bool>(
                  context: context,
                  builder: (_) => AlertDialog(
                    title: const Text('Sign Out'),
                    content: const Text(
                        'Are you sure you want to sign out?'),
                    actions: [
                      TextButton(
                        onPressed: () => Navigator.pop(context, false),
                        child: const Text('Cancel'),
                      ),
                      TextButton(
                        onPressed: () => Navigator.pop(context, true),
                        style: TextButton.styleFrom(
                            foregroundColor: AppColors.error),
                        child: const Text('Sign Out'),
                      ),
                    ],
                  ),
                );
                if (confirmed == true) {
                  await AuthService.signOut();
                  // This screen is pushed on top of the app; pop back to the
                  // root so the sign-out lands on the login screen instead of
                  // leaving this (now stale) screen showing.
                  if (context.mounted) {
                    Navigator.of(context).popUntil((r) => r.isFirst);
                  }
                }
              },
            ),

            const SizedBox(height: 24),

            // ── Family sharing note ──────────────────────────────────────
            Container(
              padding: const EdgeInsets.all(14),
              decoration: BoxDecoration(
                color: AppColors.surfaceContainerLow,
                borderRadius: BorderRadius.circular(14),
                border: Border.all(
                    color: AppColors.outlineVariant.withValues(alpha: 0.5)),
              ),
              child: Row(
                children: [
                  const Icon(Icons.info_outline_rounded,
                      size: 16, color: AppColors.onSurfaceVariant),
                  const SizedBox(width: 10),
                  Expanded(
                    child: Text(
                      isAdmin
                          ? 'Share your Fridge ID so family can connect on their '
                              'own devices. New members appear here as a request '
                              'to approve. The first person to connect a fridge '
                              'becomes its manager.'
                          : 'You are a member. You can use every feature except '
                              'managing members and changing alert settings. '
                              'Contact the manager for those.',
                      style: const TextStyle(
                          fontSize: 12,
                          color: AppColors.onSurfaceVariant),
                    ),
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

class _RoleChip extends StatelessWidget {
  final bool isAdmin;

  const _RoleChip({required this.isAdmin});

  @override
  Widget build(BuildContext context) {
    final color = isAdmin ? AppColors.primary : AppColors.secondary;
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 5),
      decoration: BoxDecoration(
        color: color.withValues(alpha: 0.14),
        borderRadius: BorderRadius.circular(20),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          Icon(isAdmin ? Icons.shield_rounded : Icons.person_rounded,
              size: 14, color: color),
          const SizedBox(width: 6),
          Text(
            isAdmin ? 'Manager' : 'Member',
            style: TextStyle(
                fontSize: 12, fontWeight: FontWeight.w700, color: color),
          ),
        ],
      ),
    );
  }
}

class _FridgeIdCard extends StatelessWidget {
  final String fridgeId;

  const _FridgeIdCard({required this.fridgeId});

  @override
  Widget build(BuildContext context) {
    return Container(
      width: double.infinity,
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: AppColors.surfaceContainerLow,
        borderRadius: BorderRadius.circular(16),
        border: Border.all(
            color: AppColors.outlineVariant.withValues(alpha: 0.5)),
      ),
      child: Row(
        children: [
          const Icon(Icons.kitchen_rounded, color: AppColors.primary, size: 22),
          const SizedBox(width: 12),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                const Text('Fridge ID',
                    style: TextStyle(
                        fontSize: 11, color: AppColors.onSurfaceVariant)),
                Text(fridgeId,
                    style: const TextStyle(
                        fontSize: 16,
                        fontWeight: FontWeight.w800,
                        color: AppColors.onSurface)),
                const Text('Share this so family can join your fridge',
                    style: TextStyle(
                        fontSize: 11, color: AppColors.onSurfaceVariant)),
              ],
            ),
          ),
        ],
      ),
    );
  }
}

class _MembersList extends StatelessWidget {
  final String currentUid;

  const _MembersList({required this.currentUid});

  @override
  Widget build(BuildContext context) {
    return StreamBuilder<List<Member>>(
      stream: FridgeSession.membersStream(),
      builder: (context, snap) {
        final members = snap.data ?? [];
        if (members.isEmpty) {
          return const Padding(
            padding: EdgeInsets.symmetric(vertical: 8),
            child: Text('No members yet.',
                style: TextStyle(color: AppColors.onSurfaceVariant)),
          );
        }
        final pending = members.where((m) => m.isPending).toList();
        final active = members.where((m) => !m.isPending).toList();
        final adminCount = active.where((m) => m.isAdmin).length;

        return Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            if (pending.isNotEmpty) ...[
              const Text('Requests to join',
                  style: TextStyle(
                      fontSize: 12,
                      fontWeight: FontWeight.w700,
                      color: AppColors.error)),
              const SizedBox(height: 8),
              ...pending.map((m) => _MemberTile(
                    member: m,
                    isSelf: m.uid == currentUid,
                    isLastAdmin: false,
                  )),
              const SizedBox(height: 12),
            ],
            ...active.map((m) => _MemberTile(
                  member: m,
                  isSelf: m.uid == currentUid,
                  isLastAdmin: m.isAdmin && adminCount <= 1,
                )),
          ],
        );
      },
    );
  }
}

class _MemberTile extends StatelessWidget {
  final Member member;
  final bool isSelf;
  final bool isLastAdmin;

  const _MemberTile({
    required this.member,
    required this.isSelf,
    required this.isLastAdmin,
  });

  @override
  Widget build(BuildContext context) {
    return Container(
      margin: const EdgeInsets.only(bottom: 8),
      padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 12),
      decoration: BoxDecoration(
        color: AppColors.surfaceContainerLowest,
        borderRadius: BorderRadius.circular(14),
        border: Border.all(
            color: member.isPending
                ? AppColors.error.withValues(alpha: 0.4)
                : AppColors.outlineVariant.withValues(alpha: 0.4)),
      ),
      child: Row(
        children: [
          CircleAvatar(
            radius: 18,
            backgroundColor: member.isAdmin
                ? AppColors.primaryFixed
                : AppColors.secondaryContainer,
            child: Text(
              member.displayName.isNotEmpty
                  ? member.displayName[0].toUpperCase()
                  : '?',
              style: TextStyle(
                  fontSize: 15,
                  fontWeight: FontWeight.w700,
                  color: member.isAdmin
                      ? AppColors.primary
                      : AppColors.onSecondaryContainer),
            ),
          ),
          const SizedBox(width: 12),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Row(
                  children: [
                    Flexible(
                      child: Text(
                        member.displayName.isNotEmpty
                            ? member.displayName
                            : member.email,
                        style: const TextStyle(
                            fontSize: 14, fontWeight: FontWeight.w600),
                        overflow: TextOverflow.ellipsis,
                      ),
                    ),
                    if (isSelf)
                      const Padding(
                        padding: EdgeInsets.only(left: 6),
                        child: Text('(you)',
                            style: TextStyle(
                                fontSize: 11,
                                color: AppColors.onSurfaceVariant)),
                      ),
                  ],
                ),
                Text(
                  member.isPending
                      ? 'Requested access'
                      : member.isAdmin
                          ? 'Manager'
                          : 'Member',
                  style: TextStyle(
                      fontSize: 11,
                      color: member.isPending
                          ? AppColors.error
                          : member.isAdmin
                              ? AppColors.primary
                              : AppColors.onSurfaceVariant),
                ),
              ],
            ),
          ),

          // Pending → Approve / Deny. Active → role menu.
          if (member.isPending)
            Row(
              mainAxisSize: MainAxisSize.min,
              children: [
                IconButton(
                  tooltip: 'Approve',
                  icon: const Icon(Icons.check_circle_rounded,
                      color: AppColors.secondary),
                  onPressed: () => FridgeSession.approveMember(member.uid),
                ),
                IconButton(
                  tooltip: 'Decline',
                  icon: const Icon(Icons.cancel_rounded,
                      color: AppColors.error),
                  onPressed: () => FridgeSession.removeMember(member.uid),
                ),
              ],
            )
          else
            PopupMenuButton<String>(
              icon: const Icon(Icons.more_vert_rounded,
                  color: AppColors.onSurfaceVariant),
              onSelected: (v) async {
                if (v == 'make_admin') {
                  await FridgeSession.setMemberRole(member.uid, 'admin');
                } else if (v == 'make_family') {
                  await FridgeSession.setMemberRole(member.uid, 'family');
                } else if (v == 'remove') {
                  await FridgeSession.removeMember(member.uid);
                }
              },
              itemBuilder: (_) => [
                if (!member.isAdmin)
                  const PopupMenuItem(
                      value: 'make_admin', child: Text('Make Manager')),
                if (member.isAdmin && !isLastAdmin)
                  const PopupMenuItem(
                      value: 'make_family',
                      child: Text('Change to Member')),
                if (!isSelf)
                  const PopupMenuItem(
                      value: 'remove',
                      child: Text('Remove access',
                          style: TextStyle(color: AppColors.error))),
              ],
            ),
        ],
      ),
    );
  }
}

class _ActionTile extends StatelessWidget {
  final IconData icon;
  final String label;
  final String subtitle;
  final Color color;
  final VoidCallback onTap;

  const _ActionTile({
    required this.icon,
    required this.label,
    required this.subtitle,
    this.color = AppColors.primary,
    required this.onTap,
  });

  @override
  Widget build(BuildContext context) {
    return GestureDetector(
      onTap: onTap,
      child: Container(
        padding: const EdgeInsets.all(16),
        decoration: BoxDecoration(
          color: AppColors.surfaceContainerLowest,
          borderRadius: BorderRadius.circular(16),
          border: Border.all(
              color: AppColors.outlineVariant.withValues(alpha: 0.4)),
          boxShadow: const [
            BoxShadow(
                color: Color(0x08000000),
                blurRadius: 6,
                offset: Offset(0, 2)),
          ],
        ),
        child: Row(
          children: [
            Container(
              width: 42,
              height: 42,
              decoration: BoxDecoration(
                color: color.withValues(alpha: 0.12),
                shape: BoxShape.circle,
              ),
              child: Icon(icon, color: color, size: 20),
            ),
            const SizedBox(width: 14),
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(label,
                      style: TextStyle(
                          fontSize: 15,
                          fontWeight: FontWeight.w600,
                          color: color)),
                  Text(subtitle,
                      style: const TextStyle(
                          fontSize: 12,
                          color: AppColors.onSurfaceVariant)),
                ],
              ),
            ),
            const Icon(Icons.chevron_right_rounded,
                color: AppColors.onSurfaceVariant),
          ],
        ),
      ),
    );
  }
}
