import 'dart:async';
import 'package:flutter/material.dart';
import '../models/fridge_item.dart';
import '../services/fridge_service.dart';
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

  // Fire the door alert if it stays open this long (matches the CH buzzer).
  static const _doorOpenAlert = Duration(seconds: 30);

  @override
  void initState() {
    super.initState();

    // Expiry (#6): reschedule OS notifications on every inventory change.
    _invSub = FridgeService.inventoryStream().listen((inv) {
      NotificationService.scheduleExpiryAlerts(inv?.items ?? const []);
    });

    // Temperature (#9): notify on breach, reset when it recovers.
    _tempSub = FridgeService.temperatureStream().listen((t) {
      if (t == null) return;
      if (t.isAlert && t.temperatureC != null) {
        NotificationService.notifyTemperature(t.temperatureC!);
      } else if (t.isOk) {
        NotificationService.clearTemperature();
      }
    });

    // Door (#10): start a timer when it opens; alert if still open at the
    // end. The doc only updates on state change, so we time it ourselves.
    _doorSub = FridgeService.doorStream().listen((d) {
      if (d == null) return;
      if (d.isOpen) {
        _doorOpenTimer ??= Timer(_doorOpenAlert, () {
          NotificationService.notifyDoorOpen();
          _doorOpenTimer = null;
        });
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
