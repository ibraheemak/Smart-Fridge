import 'dart:async';
import 'package:flutter/material.dart';
import '../models/fridge_item.dart';
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
  Timer? _doorOpenTimer;

  @override
  void initState() {
    super.initState();

    // Keep the shared fridge settings (thresholds + alert toggles) mirrored
    // from the fridge screen via RTDB.
    FridgeSettingsService.init();

    // Expiry (#6): reschedule OS notifications on every inventory change.
    _invSub = FridgeService.inventoryStream().listen((inv) {
      NotificationService.scheduleExpiryAlerts(inv?.items ?? const []);
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
            NotificationService.notifyDoorOpen();
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
