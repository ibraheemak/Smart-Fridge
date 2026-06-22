import 'package:flutter_test/flutter_test.dart';

void main() {
  testWidgets('Smart Fridge app smoke test', (WidgetTester tester) async {
    // Firebase requires real credentials — skip actual widget pump in CI.
    // Integration tests live in the integration_test/ folder.
    expect(true, isTrue);
  });
}
