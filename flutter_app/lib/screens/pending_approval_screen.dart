import 'package:cloud_firestore/cloud_firestore.dart';
import 'package:flutter/material.dart';
import '../services/auth_service.dart';
import '../services/fridge_session.dart';
import '../theme/app_theme.dart';

/// Shown while a member's join request awaits the manager's approval. Watches
/// the user's own membership doc and calls [onResolved] the moment the manager
/// approves (status → active) or declines (doc removed).
class PendingApprovalScreen extends StatefulWidget {
  final VoidCallback onResolved;

  const PendingApprovalScreen({super.key, required this.onResolved});

  @override
  State<PendingApprovalScreen> createState() => _PendingApprovalScreenState();
}

class _PendingApprovalScreenState extends State<PendingApprovalScreen> {
  @override
  Widget build(BuildContext context) {
    final stream = FridgeSession.myMembershipStream();

    return Scaffold(
      backgroundColor: AppColors.background,
      body: SafeArea(
        child: StreamBuilder<DocumentSnapshot<Map<String, dynamic>>>(
          stream: stream,
          builder: (context, snap) {
            // Declined/removed → membership doc gone → back to link screen.
            if (snap.hasData && !(snap.data?.exists ?? true)) {
              WidgetsBinding.instance
                  .addPostFrameCallback((_) => widget.onResolved());
            }
            // Approved → status flips to active.
            if (snap.data?.data()?['status'] == 'active') {
              WidgetsBinding.instance
                  .addPostFrameCallback((_) => widget.onResolved());
            }

            return Padding(
              padding: const EdgeInsets.all(24),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.center,
                children: [
                  Align(
                    alignment: Alignment.topRight,
                    child: TextButton.icon(
                      onPressed: () => AuthService.signOut(),
                      icon: const Icon(Icons.logout_rounded,
                          size: 16, color: AppColors.onSurfaceVariant),
                      label: const Text('Sign out',
                          style:
                              TextStyle(color: AppColors.onSurfaceVariant)),
                    ),
                  ),
                  const Spacer(),
                  Container(
                    width: 84,
                    height: 84,
                    decoration: BoxDecoration(
                      color: AppColors.tertiaryFixed,
                      borderRadius: BorderRadius.circular(24),
                    ),
                    child: const Icon(Icons.hourglass_top_rounded,
                        size: 44, color: Color(0xFF5B4300)),
                  ),
                  const SizedBox(height: 24),
                  Text('Waiting for approval',
                      style: Theme.of(context)
                          .textTheme
                          .headlineMedium
                          ?.copyWith(color: AppColors.onSurface),
                      textAlign: TextAlign.center),
                  const SizedBox(height: 8),
                  Text(
                    'You\'ve requested to join fridge '
                    '"${FridgeSession.fridgeId ?? ''}". The manager needs to '
                    'approve you before you can access it.',
                    textAlign: TextAlign.center,
                    style: const TextStyle(
                        fontSize: 14,
                        color: AppColors.onSurfaceVariant,
                        height: 1.4),
                  ),
                  const SizedBox(height: 20),
                  const SizedBox(
                    width: 24,
                    height: 24,
                    child: CircularProgressIndicator(
                        strokeWidth: 2, color: AppColors.primary),
                  ),
                  const Spacer(),
                  SizedBox(
                    width: double.infinity,
                    child: OutlinedButton(
                      onPressed: () async {
                        await FridgeSession.unlink();
                        widget.onResolved();
                      },
                      style: OutlinedButton.styleFrom(
                        padding: const EdgeInsets.symmetric(vertical: 14),
                        side: const BorderSide(color: AppColors.outlineVariant),
                        shape: RoundedRectangleBorder(
                            borderRadius: BorderRadius.circular(14)),
                      ),
                      child: const Text('Cancel request / try another fridge',
                          style: TextStyle(color: AppColors.onSurfaceVariant)),
                    ),
                  ),
                  const Spacer(),
                ],
              ),
            );
          },
        ),
      ),
    );
  }
}
