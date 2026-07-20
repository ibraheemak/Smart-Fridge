# Multi-fridge — how users connect to a fridge

## Model

The app supports many fridges. Each account is linked to **one** fridge.

- **`users/{uid}`** — `{ fridgeId, email, displayName }`. Which fridge this
  account works with.
- **`fridges/{fridgeId}/members/{uid}`** — `{ email, displayName, role, status,
  joinedAt }`. `role` = `admin` (manager) | `family` (member); `status` =
  `active` | `pending`.
- Everything the app reads/writes is scoped to `fridges/{fridgeId}/…` where
  `fridgeId` comes from the session (`FridgeSession.fridgeId`), not a constant.

## Flow (`FridgeSession` + the session gate in `main.dart`)

1. **Sign in** → `FridgeSession.resolve()` reads `users/{uid}.fridgeId`.
   - Legacy back-compat: a user with a member doc under the default fridge but
     no `users/` record is auto-adopted into it (existing single-fridge users
     keep working).
2. **No fridge yet** → `ConnectFridgeScreen`: the user types the fridge ID shown
   on the fridge's screen (firmware `FRIDGE_ID`).
   - `linkFridge()` checks the fridge's members: if there's **no active
     manager**, the user becomes `admin` + `active` (the manager). Otherwise
     they're added `family` + `pending`.
3. **Pending** → `PendingApprovalScreen` watches the user's own member doc and
   advances automatically when the manager approves (`status → active`) or is
   declined (doc removed → back to the connect screen).
4. **Active** → the app, scoped to that fridge.

The manager approves/declines from the **Account** screen (a "Requests to join"
section) and can promote/demote members or remove access. The last manager
can't be demoted or removed.

## Firmware

Each physical fridge's ESP32 boards (`SmartFridge_ESP32_CH` + its
`SmartFridge_ESP32_CAM`s) must be flashed with the **same, unique**
`FRIDGE_ID` (`parameters.h`). For more than one fridge, give each a distinct
serial (e.g. `SF-0001`) — ideally shown on the TFT so users can read it to
connect.

## Firestore security rules (must be set in the console — not in this repo)

The app trusts the client today. For real multi-tenant isolation, add rules so a
user can only read/write the fridge they're an **active member** of, and only
managers can write member docs:

```
match /databases/{db}/documents {
  function member(fid) {
    return get(/databases/$(db)/documents/fridges/$(fid)/members/$(request.auth.uid));
  }
  function isActiveMember(fid) {
    return request.auth != null && member(fid).data.status == 'active';
  }
  function isManager(fid) {
    return isActiveMember(fid) && member(fid).data.role == 'admin';
  }

  match /users/{uid} {
    allow read, write: if request.auth != null && request.auth.uid == uid;
  }

  match /fridges/{fid}/{document=**} {
    allow read: if isActiveMember(fid);
  }
  match /fridges/{fid}/members/{uid} {
    // A user may create their own pending/first-manager membership…
    allow create: if request.auth != null && request.auth.uid == uid;
    // …read the roster if they're a member, and only managers change others.
    allow read:   if isActiveMember(fid) || request.auth.uid == uid;
    allow update, delete: if isManager(fid) || request.auth.uid == uid;
  }
  // Inventory / sensors / scans etc. — members read, and the ESP32 (service
  // account / its own auth) writes. Tighten writes to your device auth model.
}
```

> These are a starting point — review against how the ESP32 authenticates to
> Firestore before enabling, so devices aren't locked out.
