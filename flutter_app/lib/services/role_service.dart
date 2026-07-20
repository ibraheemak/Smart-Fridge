import 'package:cloud_firestore/cloud_firestore.dart';
import 'package:firebase_auth/firebase_auth.dart';
import '../config.dart';
import 'auth_service.dart';

/// A person with access to this fridge.
class Member {
  final String uid;
  final String email;
  final String displayName;
  final String role; // "admin" (Homeowner) | "family"
  final DateTime? joinedAt;

  const Member({
    required this.uid,
    required this.email,
    required this.displayName,
    required this.role,
    this.joinedAt,
  });

  bool get isAdmin => role == 'admin';

  factory Member.fromDoc(String uid, Map<String, dynamic> m) => Member(
        uid: uid,
        email: m['email'] as String? ?? '',
        displayName: m['displayName'] as String? ?? '',
        role: m['role'] as String? ?? 'family',
        joinedAt: DateTime.tryParse(m['joinedAt'] as String? ?? ''),
      );
}

/// Two-role access control for the fridge.
///
/// Membership lives in `fridges/{id}/members/{uid}`. The FIRST person to
/// sign in (no admin exists yet) becomes the Homeowner (admin); everyone
/// after is a Family member. The admin can promote/demote from the Account
/// screen. Family members can use every feature except user management,
/// alert-threshold settings, and icon regeneration.
class RoleService {
  static final _db = FirebaseFirestore.instance;

  static CollectionReference<Map<String, dynamic>> get _membersRef => _db
      .collection('fridges')
      .doc(AppConfig.fridgeId)
      .collection('members');

  static String? _cachedRole;

  static bool get isAdmin => _cachedRole == 'admin';
  static bool get isFamily => _cachedRole == 'family';
  static bool get isResolved => _cachedRole != null;
  static String get role => _cachedRole ?? 'family';

  /// Resolve the signed-in user's role, creating their membership on first
  /// sign-in. Idempotent — safe to call on every launch. Call after auth
  /// and before showing the main UI.
  static Future<void> resolveRole() async {
    final user = FirebaseAuth.instance.currentUser;
    if (user == null) {
      _cachedRole = null;
      return;
    }

    final mine = await _membersRef.doc(user.uid).get();
    if (mine.exists) {
      _cachedRole = mine.data()?['role'] as String? ?? 'family';
      return;
    }

    // No membership yet: become admin only if no admin exists for this fridge.
    final admins =
        await _membersRef.where('role', isEqualTo: 'admin').limit(1).get();
    final role = admins.docs.isEmpty ? 'admin' : 'family';

    await _membersRef.doc(user.uid).set({
      'email': user.email ?? '',
      'displayName': AuthService.displayName,
      'role': role,
      'joinedAt': DateTime.now().toIso8601String(),
    });
    _cachedRole = role;
  }

  /// Live list of everyone with access (admin screen).
  static Stream<List<Member>> membersStream() => _membersRef
      .orderBy('joinedAt')
      .snapshots()
      .map((s) => s.docs.map((d) => Member.fromDoc(d.id, d.data())).toList());

  static Future<void> setMemberRole(String uid, String role) =>
      _membersRef.doc(uid).update({'role': role});

  static Future<void> removeMember(String uid) =>
      _membersRef.doc(uid).delete();

  static void clear() => _cachedRole = null;
}
