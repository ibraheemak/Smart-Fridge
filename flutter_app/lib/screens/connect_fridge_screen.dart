import 'package:flutter/material.dart';
import '../services/auth_service.dart';
import '../services/fridge_session.dart';
import '../theme/app_theme.dart';

/// One-time onboarding: link the account to a fridge by its device ID.
/// The first person to link a fridge becomes its manager; anyone after joins
/// as a member pending the manager's approval.
class ConnectFridgeScreen extends StatefulWidget {
  final bool wasDeclined;
  final VoidCallback onLinked;

  const ConnectFridgeScreen({
    super.key,
    required this.onLinked,
    this.wasDeclined = false,
  });

  @override
  State<ConnectFridgeScreen> createState() => _ConnectFridgeScreenState();
}

class _ConnectFridgeScreenState extends State<ConnectFridgeScreen> {
  final _ctrl = TextEditingController();
  bool _busy = false;
  String? _error;

  @override
  void dispose() {
    _ctrl.dispose();
    super.dispose();
  }

  Future<void> _link() async {
    final id = _ctrl.text.trim();
    if (id.isEmpty) {
      setState(() => _error = 'Enter the fridge ID shown on the fridge screen.');
      return;
    }
    setState(() {
      _busy = true;
      _error = null;
    });
    try {
      await FridgeSession.linkFridge(id);
      widget.onLinked();
    } catch (e) {
      if (mounted) {
        setState(() {
          _busy = false;
          _error = e.toString().replaceFirst('Exception: ', '');
        });
      }
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: AppColors.background,
      body: SafeArea(
        child: Padding(
          padding: const EdgeInsets.all(24),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Align(
                alignment: Alignment.topRight,
                child: TextButton.icon(
                  onPressed: () => AuthService.signOut(),
                  icon: const Icon(Icons.logout_rounded,
                      size: 16, color: AppColors.onSurfaceVariant),
                  label: const Text('Sign out',
                      style: TextStyle(color: AppColors.onSurfaceVariant)),
                ),
              ),
              const Spacer(),
              Center(
                child: Container(
                  width: 84,
                  height: 84,
                  decoration: BoxDecoration(
                    color: AppColors.primaryFixed,
                    borderRadius: BorderRadius.circular(24),
                  ),
                  child: const Icon(Icons.kitchen_rounded,
                      size: 44, color: AppColors.primary),
                ),
              ),
              const SizedBox(height: 24),
              Text('Connect your fridge',
                  style: Theme.of(context)
                      .textTheme
                      .headlineMedium
                      ?.copyWith(color: AppColors.onSurface)),
              const SizedBox(height: 8),
              const Text(
                'Enter the fridge ID shown on your fridge\'s screen. The first '
                'person to connect becomes the manager; others join as members '
                'once the manager approves them.',
                style: TextStyle(
                    fontSize: 14, color: AppColors.onSurfaceVariant, height: 1.4),
              ),
              if (widget.wasDeclined) ...[
                const SizedBox(height: 16),
                Container(
                  padding: const EdgeInsets.all(12),
                  decoration: BoxDecoration(
                    color: AppColors.errorContainer,
                    borderRadius: BorderRadius.circular(12),
                  ),
                  child: const Row(
                    children: [
                      Icon(Icons.info_outline_rounded,
                          size: 18, color: AppColors.error),
                      SizedBox(width: 10),
                      Expanded(
                        child: Text(
                          'Your previous request was declined or removed. You '
                          'can request access again.',
                          style: TextStyle(fontSize: 13, color: AppColors.error),
                        ),
                      ),
                    ],
                  ),
                ),
              ],
              const SizedBox(height: 24),
              TextField(
                controller: _ctrl,
                autocorrect: false,
                textInputAction: TextInputAction.done,
                onSubmitted: (_) => _link(),
                decoration: InputDecoration(
                  labelText: 'Fridge ID',
                  hintText: 'e.g. fridge1',
                  prefixIcon: const Icon(Icons.tag_rounded),
                  errorText: _error,
                ),
              ),
              const SizedBox(height: 20),
              SizedBox(
                width: double.infinity,
                child: FilledButton(
                  onPressed: _busy ? null : _link,
                  style: FilledButton.styleFrom(
                    padding: const EdgeInsets.symmetric(vertical: 16),
                    shape: RoundedRectangleBorder(
                        borderRadius: BorderRadius.circular(14)),
                  ),
                  child: _busy
                      ? const SizedBox(
                          width: 20,
                          height: 20,
                          child: CircularProgressIndicator(
                              strokeWidth: 2, color: Colors.white))
                      : const Text('Connect'),
                ),
              ),
              const Spacer(),
              const Spacer(),
            ],
          ),
        ),
      ),
    );
  }
}
