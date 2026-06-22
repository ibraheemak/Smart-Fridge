import 'package:firebase_auth/firebase_auth.dart';

class AuthService {
  static final _auth = FirebaseAuth.instance;

  static Stream<User?> get authStateChanges => _auth.authStateChanges();
  static User? get currentUser => _auth.currentUser;

  static String get displayName {
    final u = _auth.currentUser;
    if (u == null) return 'Guest';
    return u.displayName?.isNotEmpty == true
        ? u.displayName!
        : u.email?.split('@').first ?? 'User';
  }

  static Future<void> signIn(String email, String password) async {
    await _auth.signInWithEmailAndPassword(
        email: email.trim(), password: password);
  }

  static Future<void> createAccount(String email, String password) async {
    await _auth.createUserWithEmailAndPassword(
        email: email.trim(), password: password);
  }

  static Future<void> signOut() async => _auth.signOut();

  static Future<void> resetPassword(String email) async {
    await _auth.sendPasswordResetEmail(email: email.trim());
  }
}
