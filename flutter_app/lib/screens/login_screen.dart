import 'package:flutter/material.dart';
import '../services/auth_service.dart';
import '../theme/app_theme.dart';

class LoginScreen extends StatefulWidget {
  const LoginScreen({super.key});

  @override
  State<LoginScreen> createState() => _LoginScreenState();
}

class _LoginScreenState extends State<LoginScreen> {
  final _emailCtrl = TextEditingController();
  final _passCtrl = TextEditingController();
  bool _obscure = true;
  bool _loading = false;
  String _error = '';
  bool _isRegister = false;

  Future<void> _submit() async {
    final email = _emailCtrl.text.trim();
    final pass = _passCtrl.text;
    if (email.isEmpty || pass.isEmpty) {
      setState(() => _error = 'Please fill in all fields.');
      return;
    }
    setState(() {
      _loading = true;
      _error = '';
    });
    try {
      if (_isRegister) {
        await AuthService.createAccount(email, pass);
      } else {
        await AuthService.signIn(email, pass);
      }
    } catch (e) {
      setState(() => _error = _friendlyError(e.toString()));
    } finally {
      if (mounted) setState(() => _loading = false);
    }
  }

  Future<void> _forgotPassword() async {
    final email = _emailCtrl.text.trim();
    if (email.isEmpty) {
      setState(() => _error = 'Enter your email first.');
      return;
    }
    try {
      await AuthService.resetPassword(email);
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(content: Text('Password reset email sent!')),
        );
      }
    } catch (e) {
      setState(() => _error = _friendlyError(e.toString()));
    }
  }

  String _friendlyError(String raw) {
    if (raw.contains('user-not-found') || raw.contains('wrong-password') ||
        raw.contains('invalid-credential')) {
      return 'Invalid email or password.';
    }
    if (raw.contains('email-already-in-use')) return 'Email already registered.';
    if (raw.contains('weak-password')) return 'Password must be at least 6 characters.';
    if (raw.contains('invalid-email')) return 'Invalid email address.';
    if (raw.contains('network-request-failed')) return 'No internet connection.';
    if (raw.contains('operation-not-allowed')) {
      return 'Email/Password sign-in is not enabled.\n'
          'Go to Firebase Console → Authentication → Sign-in method → enable Email/Password.';
    }
    if (raw.contains('too-many-requests')) return 'Too many attempts. Please wait and try again.';
    // Show actual error for unknown cases
    final trimmed = raw.replaceFirst('Exception: ', '').replaceFirst('[firebase_auth/', '[');
    return trimmed.length > 120 ? '${trimmed.substring(0, 120)}…' : trimmed;
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: AppColors.background,
      body: Stack(
        fit: StackFit.expand,
        children: [
          // Decorative background gradient
          Container(
            decoration: const BoxDecoration(
              gradient: LinearGradient(
                begin: Alignment.topCenter,
                end: Alignment.bottomCenter,
                colors: [Color(0xFF1A237E), Color(0xFF283593), AppColors.background],
                stops: [0.0, 0.35, 0.65],
              ),
            ),
          ),

          SafeArea(
            child: SingleChildScrollView(
              padding: const EdgeInsets.symmetric(horizontal: 24),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
                  const SizedBox(height: 60),

                  // Logo + branding
                  Center(
                    child: Column(
                      children: [
                        Container(
                          width: 80,
                          height: 80,
                          decoration: BoxDecoration(
                            color: Colors.white.withValues(alpha: 0.15),
                            shape: BoxShape.circle,
                            border: Border.all(
                                color: Colors.white.withValues(alpha: 0.3),
                                width: 2),
                          ),
                          child: const Icon(Icons.kitchen_rounded,
                              size: 44, color: Colors.white),
                        ),
                        const SizedBox(height: 16),
                        const Text(
                          'Smart Fridge',
                          style: TextStyle(
                            color: Colors.white,
                            fontSize: 28,
                            fontWeight: FontWeight.w800,
                            letterSpacing: -0.5,
                          ),
                        ),
                        const SizedBox(height: 6),
                        Text(
                          'Smart and precise kitchen management',
                          style: TextStyle(
                            color: Colors.white.withValues(alpha: 0.75),
                            fontSize: 13,
                          ),
                        ),
                      ],
                    ),
                  ),

                  const SizedBox(height: 48),

                  // Card
                  Container(
                    padding: const EdgeInsets.all(24),
                    decoration: BoxDecoration(
                      color: Colors.white,
                      borderRadius: BorderRadius.circular(24),
                      boxShadow: const [
                        BoxShadow(
                            color: Color(0x1A000000),
                            blurRadius: 24,
                            offset: Offset(0, 8)),
                      ],
                    ),
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.stretch,
                      children: [
                        Text(
                          _isRegister ? 'Create Account' : 'Sign In',
                          style: const TextStyle(
                            fontSize: 22,
                            fontWeight: FontWeight.w700,
                            color: AppColors.onSurface,
                          ),
                        ),
                        const SizedBox(height: 4),
                        Text(
                          _isRegister
                              ? 'Create your family account'
                              : 'Welcome back! Please sign in.',
                          style: const TextStyle(
                              fontSize: 13, color: AppColors.onSurfaceVariant),
                        ),
                        const SizedBox(height: 24),

                        // Email
                        TextField(
                          controller: _emailCtrl,
                          keyboardType: TextInputType.emailAddress,
                          textInputAction: TextInputAction.next,
                          decoration: InputDecoration(
                            labelText: 'Email',
                            prefixIcon: const Icon(Icons.email_outlined,
                                color: AppColors.primary),
                            border: OutlineInputBorder(
                                borderRadius: BorderRadius.circular(12)),
                            enabledBorder: OutlineInputBorder(
                              borderRadius: BorderRadius.circular(12),
                              borderSide: const BorderSide(
                                  color: AppColors.outlineVariant),
                            ),
                          ),
                        ),
                        const SizedBox(height: 16),

                        // Password
                        TextField(
                          controller: _passCtrl,
                          obscureText: _obscure,
                          onSubmitted: (_) => _submit(),
                          decoration: InputDecoration(
                            labelText: 'Password',
                            prefixIcon: const Icon(Icons.lock_outline_rounded,
                                color: AppColors.primary),
                            suffixIcon: IconButton(
                              onPressed: () =>
                                  setState(() => _obscure = !_obscure),
                              icon: Icon(
                                _obscure
                                    ? Icons.visibility_outlined
                                    : Icons.visibility_off_outlined,
                                color: AppColors.onSurfaceVariant,
                              ),
                            ),
                            border: OutlineInputBorder(
                                borderRadius: BorderRadius.circular(12)),
                            enabledBorder: OutlineInputBorder(
                              borderRadius: BorderRadius.circular(12),
                              borderSide: const BorderSide(
                                  color: AppColors.outlineVariant),
                            ),
                          ),
                        ),

                        if (!_isRegister) ...[
                          const SizedBox(height: 8),
                          Align(
                            alignment: Alignment.centerRight,
                            child: TextButton(
                              onPressed: _forgotPassword,
                              child: const Text('Forgot Password?',
                                  style: TextStyle(color: AppColors.primary)),
                            ),
                          ),
                        ] else
                          const SizedBox(height: 16),

                        // Error message
                        if (_error.isNotEmpty) ...[
                          Container(
                            padding: const EdgeInsets.all(12),
                            decoration: BoxDecoration(
                              color: AppColors.errorContainer,
                              borderRadius: BorderRadius.circular(10),
                            ),
                            child: Text(
                              _error,
                              style: const TextStyle(
                                  fontSize: 13,
                                  color: AppColors.onErrorContainer),
                            ),
                          ),
                          const SizedBox(height: 16),
                        ],

                        // Submit button
                        FilledButton(
                          onPressed: _loading ? null : _submit,
                          style: FilledButton.styleFrom(
                            padding: const EdgeInsets.symmetric(vertical: 16),
                            shape: RoundedRectangleBorder(
                                borderRadius: BorderRadius.circular(14)),
                          ),
                          child: _loading
                              ? const SizedBox(
                                  width: 20,
                                  height: 20,
                                  child: CircularProgressIndicator(
                                      strokeWidth: 2, color: Colors.white))
                              : Text(_isRegister ? 'Create Account' : 'Sign In',
                                  style: const TextStyle(
                                      fontSize: 16,
                                      fontWeight: FontWeight.w700)),
                        ),

                        const SizedBox(height: 20),

                        // Toggle register/login
                        Row(
                          mainAxisAlignment: MainAxisAlignment.center,
                          children: [
                            Text(
                              _isRegister
                                  ? 'Already have an account? '
                                  : "Don't have an account? ",
                              style: const TextStyle(
                                  fontSize: 13,
                                  color: AppColors.onSurfaceVariant),
                            ),
                            GestureDetector(
                              onTap: () => setState(() {
                                _isRegister = !_isRegister;
                                _error = '';
                              }),
                              child: Text(
                                _isRegister ? 'Sign In' : 'Register now',
                                style: const TextStyle(
                                  fontSize: 13,
                                  fontWeight: FontWeight.w700,
                                  color: AppColors.primary,
                                ),
                              ),
                            ),
                          ],
                        ),
                      ],
                    ),
                  ),
                  const SizedBox(height: 32),
                ],
              ),
            ),
          ),
        ],
      ),
    );
  }

  @override
  void dispose() {
    _emailCtrl.dispose();
    _passCtrl.dispose();
    super.dispose();
  }
}
