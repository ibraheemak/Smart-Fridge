import 'package:flutter/material.dart';
import 'package:firebase_core/firebase_core.dart';
import 'package:firebase_auth/firebase_auth.dart';
import 'firebase_options.dart';
import 'services/auth_service.dart';
import 'services/notification_service.dart';
import 'services/role_service.dart';
import 'services/settings_service.dart';
import 'theme/app_theme.dart';
import 'screens/login_screen.dart';
import 'screens/main_scaffold.dart';

void main() async {
  WidgetsFlutterBinding.ensureInitialized();
  await Firebase.initializeApp(
    options: DefaultFirebaseOptions.currentPlatform,
  );
  await SettingsService.init();
  await NotificationService.init();
  runApp(const SmartFridgeApp());
}

class SmartFridgeApp extends StatelessWidget {
  const SmartFridgeApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Smart Fridge',
      theme: AppTheme.theme,
      debugShowCheckedModeBanner: false,
      home: StreamBuilder<User?>(
        stream: AuthService.authStateChanges,
        builder: (context, snap) {
          if (snap.connectionState == ConnectionState.waiting) {
            return const _Loading();
          }
          if (snap.data != null) {
            return const _RoleGate();
          }
          RoleService.clear();
          return const LoginScreen();
        },
      ),
    );
  }
}

/// Resolves the signed-in user's role (creating their membership on first
/// sign-in) before showing the app. Re-resolves whenever the uid changes.
class _RoleGate extends StatefulWidget {
  const _RoleGate();

  @override
  State<_RoleGate> createState() => _RoleGateState();
}

class _RoleGateState extends State<_RoleGate> {
  late Future<void> _resolve;

  @override
  void initState() {
    super.initState();
    _resolve = RoleService.resolveRole();
  }

  @override
  Widget build(BuildContext context) {
    return FutureBuilder<void>(
      future: _resolve,
      builder: (context, snap) {
        if (snap.connectionState != ConnectionState.done) {
          return const _Loading();
        }
        return const MainScaffold();
      },
    );
  }
}

class _Loading extends StatelessWidget {
  const _Loading();

  @override
  Widget build(BuildContext context) {
    return const Scaffold(
      body: Center(
        child: CircularProgressIndicator(color: Color(0xFF1A237E)),
      ),
    );
  }
}
