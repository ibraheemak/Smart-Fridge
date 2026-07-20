import 'package:flutter/material.dart';
import 'package:firebase_core/firebase_core.dart';
import 'package:firebase_auth/firebase_auth.dart';
import 'firebase_options.dart';
import 'services/auth_service.dart';
import 'services/fridge_session.dart';
import 'services/notification_service.dart';
import 'services/settings_service.dart';
import 'theme/app_theme.dart';
import 'screens/login_screen.dart';
import 'screens/main_scaffold.dart';
import 'screens/connect_fridge_screen.dart';
import 'screens/pending_approval_screen.dart';

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
            return const _SessionGate();
          }
          FridgeSession.clear();
          return const LoginScreen();
        },
      ),
    );
  }
}

/// Resolves which fridge the signed-in user belongs to and gates the app on
/// their membership state: not linked → connect screen, pending → waiting
/// screen, active → the app.
class _SessionGate extends StatefulWidget {
  const _SessionGate();

  @override
  State<_SessionGate> createState() => _SessionGateState();
}

class _SessionGateState extends State<_SessionGate> {
  late Future<void> _resolve;

  @override
  void initState() {
    super.initState();
    _resolve = FridgeSession.resolve();
  }

  void _reResolve() {
    // Block body: an arrow closure would RETURN the Future from the
    // assignment, and setState rejects a callback that returns a Future.
    setState(() {
      _resolve = FridgeSession.resolve();
    });
  }

  @override
  Widget build(BuildContext context) {
    return FutureBuilder<void>(
      future: _resolve,
      builder: (context, snap) {
        if (snap.connectionState != ConnectionState.done) {
          return const _Loading();
        }
        switch (FridgeSession.state) {
          case SessionState.active:
            return const MainScaffold();
          case SessionState.pending:
            return PendingApprovalScreen(onResolved: _reResolve);
          case SessionState.needsLink:
          case SessionState.denied:
            return ConnectFridgeScreen(
              wasDeclined: FridgeSession.state == SessionState.denied,
              onLinked: _reResolve,
            );
        }
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
