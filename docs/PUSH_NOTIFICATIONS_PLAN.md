# Push Notifications — status & plan

## What ships now (on-device, no server)

`NotificationService` (`flutter_app/lib/services/notification_service.dart`),
driven by the alert coordinator in `MainScaffold`:

| Story | Mechanism | Fires when app is… |
|---|---|---|
| #6 Expiry Alerts | **OS-scheduled** local notifications (`zonedSchedule`), rescheduled on every inventory change, `warningDays` before expiry at 09:00 Asia/Jerusalem | open, backgrounded, **or closed** ✅ |
| #9 Temperature Alert | live local notification when `TemperatureReading.isAlert`, 30-min debounce | open or backgrounded |
| #10 Door Open Alert | 30 s timer started when the door opens; notifies if still open | open or backgrounded |

Expiry is fully covered because expiry times are known in advance and the OS
delivers them even if the app is terminated. Temperature and door are
**real-time** conditions — they can only be evaluated while the app process is
alive, so today they notify when the app is open or recently backgrounded, not
when it's been killed.

## The gap: always-on temp/door push

To alert on temperature/door when the app is fully closed, the *server* must
push. Plan:

1. **App side** — add `firebase_messaging`, request permission, store the FCM
   token at `fridges/{id}/members/{uid}.fcmToken`. Handle foreground messages
   (suppress if the coordinator already showed one) and notification taps.
2. **Cloud Function** (Firebase, `functions/`) — Firestore `onDocumentUpdated`
   triggers on:
   - `fridges/{id}/sensors/temperature` → if `temperature` outside the fridge's
     safe range, `sendEachForMulticast` to all member tokens.
   - `fridges/{id}/sensors/door` → on transition to `open`, schedule a check (or
     use a scheduled function) and push if still open after the threshold.
   Store the safe range / thresholds in `fridges/{id}/settings` (the same doc the
   reconciliation plan proposes) so the function and the app agree.
3. **Dedupe** — stamp each push with the sensor `updatedAt`; the app ignores a
   push whose condition it already surfaced locally.

Cost: one Firebase Functions deploy (Blaze plan) + ~1 app-side file. Left as a
follow-up because it needs a server deploy, not just app code.

## Notes
- Android 13+ notification permission is requested at first launch
  (`POST_NOTIFICATIONS` in the manifest).
- Expiry uses `inexactAllowWhileIdle` — no exact-alarm permission needed; a few
  hours' delivery slack is fine for a day-ahead reminder.
- Scheduled reminders are re-armed every app launch (inventory stream fires), so
  they survive reboots without a boot receiver.
