import 'package:cloud_firestore/cloud_firestore.dart';
import 'package:firebase_auth/firebase_auth.dart';
import '../config.dart';
import 'auth_service.dart';

/// A person's membership in one fridge.
class Member {
  final String uid;
  final String email;
  final String displayName;
  final String role; // "admin" (manager) | "family" (member)
  final String status; // "active" | "pending"
  final DateTime? joinedAt;

  const Member({
    required this.uid,
    required this.email,
    required this.displayName,
    required this.role,
    required this.status,
    this.joinedAt,
  });

  bool get isAdmin => role == 'admin';
  bool get isPending => status == 'pending';

  factory Member.fromDoc(String uid, Map<String, dynamic> m) => Member(
        uid: uid,
        email: m['email'] as String? ?? '',
        displayName: m['displayName'] as String? ?? '',
        role: m['role'] as String? ?? 'family',
        status: m['status'] as String? ?? 'active',
        joinedAt: DateTime.tryParse(m['joinedAt'] as String? ?? ''),
      );
}

/// Where the signed-in user stands with a fridge.
enum SessionState {
  needsLink, // not linked to any fridge yet
  pending, // linked, waiting for the manager's approval
  denied, // request was declined / membership removed — must re-link
  active, // full access
}

/// Multi-fridge session. Each user is linked to ONE fridge, recorded at
/// `users/{uid}.fridgeId`. Membership + role live at
/// `fridges/{fridgeId}/members/{uid}`. The first person to link a fridge (no
/// active manager yet) becomes its manager (admin, active); everyone after
/// joins as a member (family) with status "pending" until the manager
/// approves them. Every fridge-scoped Firestore path uses [fridgeId].
class FridgeSession {
  static final _db = FirebaseFirestore.instance;

  static String? _fridgeId;
  static String? _role;
  static SessionState _state = SessionState.needsLink;

  /// The fridge the current user is working with (null until linked+resolved).
  static String? get fridgeId => _fridgeId;
  static String get role => _role ?? 'family';
  static bool get isAdmin => _role == 'admin';
  static SessionState get state => _state;

  static DocumentReference<Map<String, dynamic>> _userRef(String uid) =>
      _db.collection('users').doc(uid);

  static CollectionReference<Map<String, dynamic>> _membersRef(String fid) =>
      _db.collection('fridges').doc(fid).collection('members');

  /// Resolve the current user's fridge + membership. Call after auth and
  /// after any link/approval change.
  static Future<void> resolve() async {
    final user = FirebaseAuth.instance.currentUser;
    if (user == null) {
      _reset();
      return;
    }

    // 1. Which fridge is this user linked to?
    final userDoc = await _userRef(user.uid).get();
    var fid = userDoc.data()?['fridgeId'] as String?;

    // Back-compat: users who joined before multi-fridge have a member doc
    // under the default fridge but no users/ record — adopt it.
    if (fid == null) {
      final legacy =
          await _membersRef(AppConfig.fridgeId).doc(user.uid).get();
      if (legacy.exists) {
        fid = AppConfig.fridgeId;
        await _userRef(user.uid).set({
          'fridgeId': fid,
          'email': user.email ?? '',
          'displayName': AuthService.displayName,
        }, SetOptions(merge: true));
      }
    }

    if (fid == null) {
      _fridgeId = null;
      _role = null;
      _state = SessionState.needsLink;
      return;
    }

    // 2. Membership under that fridge.
    final mem = await _membersRef(fid).doc(user.uid).get();
    if (!mem.exists) {
      // Linked but the membership is gone (declined or removed) — unlink so
      // the user can try again.
      await _userRef(user.uid)
          .set({'fridgeId': FieldValue.delete()}, SetOptions(merge: true));
      _fridgeId = null;
      _role = null;
      _state = SessionState.denied;
      return;
    }

    _fridgeId = fid;
    _role = mem.data()?['role'] as String? ?? 'family';
    final status = mem.data()?['status'] as String? ?? 'active';
    _state = status == 'pending' ? SessionState.pending : SessionState.active;
  }

  /// Link the current user to a fridge by its device ID. First to link a
  /// fridge with no active manager becomes the manager (active); otherwise
  /// joins as a pending member awaiting approval.
  static Future<void> linkFridge(String rawId) async {
    final user = FirebaseAuth.instance.currentUser;
    if (user == null) return;
    final fid = rawId.trim();
    if (fid.isEmpty) {
      throw Exception('Enter the fridge ID shown on the fridge screen.');
    }

    final membersRef = _membersRef(fid);
    // Members collections are household-sized — read all and decide locally
    // (no composite index needed).
    final all = await membersRef.get();
    final hasActiveManager = all.docs.any((d) {
      final m = d.data();
      return m['role'] == 'admin' && (m['status'] ?? 'active') == 'active';
    });

    final firstManager = !hasActiveManager;
    await membersRef.doc(user.uid).set({
      'email': user.email ?? '',
      'displayName': AuthService.displayName,
      'role': firstManager ? 'admin' : 'family',
      'status': firstManager ? 'active' : 'pending',
      'joinedAt': DateTime.now().toIso8601String(),
    });
    await _userRef(user.uid).set({
      'fridgeId': fid,
      'email': user.email ?? '',
      'displayName': AuthService.displayName,
    }, SetOptions(merge: true));
  }

  /// Cancel a pending request / leave the current fridge, so the user can
  /// link a different one.
  static Future<void> unlink() async {
    final user = FirebaseAuth.instance.currentUser;
    if (user == null) return;
    final fid = _fridgeId;
    if (fid != null) {
      await _membersRef(fid).doc(user.uid).delete();
    }
    await _userRef(user.uid)
        .set({'fridgeId': FieldValue.delete()}, SetOptions(merge: true));
    _fridgeId = null;
    _role = null;
    _state = SessionState.needsLink;
  }

  /// Streams this user's own membership doc — the pending screen watches it
  /// to advance the moment the manager approves (or declines).
  static Stream<DocumentSnapshot<Map<String, dynamic>>>? myMembershipStream() {
    final user = FirebaseAuth.instance.currentUser;
    final fid = _fridgeId;
    if (user == null || fid == null) return null;
    return _membersRef(fid).doc(user.uid).snapshots();
  }

  // ── Manager: member administration ─────────────────────────────────────────

  /// Everyone linked to the current fridge (active + pending).
  static Stream<List<Member>> membersStream() {
    final fid = _fridgeId;
    if (fid == null) return const Stream.empty();
    return _membersRef(fid).snapshots().map((s) {
      final list =
          s.docs.map((d) => Member.fromDoc(d.id, d.data())).toList();
      list.sort((a, b) =>
          (a.joinedAt ?? DateTime(0)).compareTo(b.joinedAt ?? DateTime(0)));
      return list;
    });
  }

  static Future<void> approveMember(String uid) =>
      _membersRef(_fridgeId!).doc(uid).update({'status': 'active'});

  static Future<void> setMemberRole(String uid, String role) =>
      _membersRef(_fridgeId!).doc(uid).update({'role': role});

  /// Decline a pending member or remove an existing one. Their app detects
  /// the missing membership on next resolve and returns to the link screen.
  static Future<void> removeMember(String uid) =>
      _membersRef(_fridgeId!).doc(uid).delete();

  static void clear() => _reset();

  static void _reset() {
    _fridgeId = null;
    _role = null;
    _state = SessionState.needsLink;
  }
}
