import 'dart:async';
import 'package:flutter/material.dart';
import '../models/fridge_item.dart';
import '../models/fridge_settings.dart';
import '../services/fridge_service.dart';
import '../services/fridge_settings_service.dart';
import '../services/notification_service.dart';
import 'home_screen.dart';
import 'inventory_screen.dart';
import 'recipes_screen.dart';
import 'shopping_screen.dart';
import 'settings_screen.dart';

class MainScaffold extends StatefulWidget {
  const MainScaffold({super.key});

  @override
  State<MainScaffold> createState() => _MainScaffoldState();
}

class _MainScaffoldState extends State<MainScaffold> {
  int _index = 0;

  // Alert coordinator: watches the live streams and drives notifications.
  late final StreamSubscription<InventorySnapshot?> _invSub;
  late final StreamSubscription<TemperatureReading?> _tempSub;
  late final StreamSubscription<DoorStatus?> _doorSub;
  StreamSubscription<FridgeSettings?>? _settingsSub;
  Timer? _doorOpenTimer;

  // Last inventory seen, so a settings change can re-time the expiry alerts
  // without waiting for the inventory doc to change.
  List<FridgeItem> _lastItems = const [];
  int? _lastWarnDays;
  bool? _lastExpiryOn;

  @override
  void initState() {
    super.initState();

    // Keep the shared fridge settings (thresholds + alert toggles) mirrored
    // from the fridge screen via RTDB.
    FridgeSettingsService.init();

    // Expiry (#6): reschedule OS notifications on every inventory change.
    _invSub = FridgeService.inventoryStream().listen((inv) {
      _lastItems = inv?.items ?? const [];
      NotificationService.scheduleExpiryAlerts(_lastItems);
    });

    // …and whenever the expiry settings themselves change (from this app or
    // the fridge screen). Without this, turning expiry alerts off or changing
    // the warn-days left the already-queued OS notifications firing at the old
    // schedule until the inventory happened to change. scheduleExpiryAlerts
    // cancels its whole ID range first, so re-running it is idempotent.
    _settingsSub = FridgeSettingsService.stream().listen((s) {
      if (s == null) return;
      if (s.expiryWarnDays != _lastWarnDays ||
          s.expiryAlertOn != _lastExpiryOn) {
        _lastWarnDays = s.expiryWarnDays;
        _lastExpiryOn = s.expiryAlertOn;
        NotificationService.scheduleExpiryAlerts(_lastItems);
      }
    });

    // Temperature (#9): notify on breach — if env alerts are enabled.
    _tempSub = FridgeService.temperatureStream().listen((t) {
      if (t == null) return;
      if (t.isAlert &&
          t.temperatureC != null &&
          FridgeSettingsService.cached.envAlertOn) {
        NotificationService.notifyTemperature(t.temperatureC!);
      } else if (t.isOk) {
        NotificationService.clearTemperature();
      }
    });

    // Door (#10): buzz-delay and toggle come from the shared settings.
    _doorSub = FridgeService.doorStream().listen((d) {
      if (d == null) return;
      if (d.isOpen && FridgeSettingsService.cached.doorAlertOn) {
        _doorOpenTimer ??= Timer(
          Duration(seconds: FridgeSettingsService.cached.doorAlertS),
          () {
            // Re-check: the manager may have switched door alerts off while
            // the timer was already running.
            if (FridgeSettingsService.cached.doorAlertOn) {
              NotificationService.notifyDoorOpen();
            }
            _doorOpenTimer = null;
          },
        );
      } else {
        _doorOpenTimer?.cancel();
        _doorOpenTimer = null;
        NotificationService.clearDoor();
      }
    });
  }

  @override
  void dispose() {
    _invSub.cancel();
    _tempSub.cancel();
    _doorSub.cancel();
    _settingsSub?.cancel();
    _doorOpenTimer?.cancel();
    super.dispose();
  }

  void _navigate(int index) => setState(() => _index = index);

  @override
  Widget build(BuildContext context) {
    final screens = [
      HomeScreen(onNavigate: _navigate),
      const InventoryScreen(),
      const RecipesScreen(),
      const ShoppingScreen(),
      const SettingsScreen(),
    ];

    return Scaffold(
      body: IndexedStack(index: _index, children: screens),
      bottomNavigationBar: NavigationBar(
        selectedIndex: _index,
        onDestinationSelected: _navigate,
        destinations: const [
          NavigationDestination(
            icon: Icon(Icons.home_outlined),
            selectedIcon: Icon(Icons.home_rounded),
            label: 'Home',
          ),
          NavigationDestination(
            icon: Icon(Icons.inventory_2_outlined),
            selectedIcon: Icon(Icons.inventory_2_rounded),
            label: 'Inventory',
          ),
          NavigationDestination(
            icon: Icon(Icons.menu_book_outlined),
            selectedIcon: Icon(Icons.menu_book_rounded),
            label: 'Recipes',
          ),
          NavigationDestination(
            icon: Icon(Icons.shopping_cart_outlined),
            selectedIcon: Icon(Icons.shopping_cart_rounded),
            label: 'Shopping',
          ),
          NavigationDestination(
            icon: Icon(Icons.settings_outlined),
            selectedIcon: Icon(Icons.settings_rounded),
            label: 'Settings',
          ),
        ],
      ),
    );
  }
}
