import 'dart:typed_data';
import 'package:flutter_test/flutter_test.dart';
import 'package:image/image.dart' as img;
import 'package:smart_fridge/services/icon_generator_service.dart';

/// Scan JPEG markers to find the Start-Of-Frame. FFC0 = baseline,
/// FFC2 = progressive. Returns the marker byte, or null.
int? _sofMarker(Uint8List b) {
  var i = 2; // skip SOI
  while (i + 3 < b.length) {
    if (b[i] != 0xFF) {
      i++;
      continue;
    }
    final m = b[i + 1];
    if (m == 0xC0 || m == 0xC1 || m == 0xC2) return m; // SOF0/1/2
    if (m == 0xD8 || m == 0xD9 || (m >= 0xD0 && m <= 0xD7)) {
      i += 2;
      continue;
    }
    final len = (b[i + 2] << 8) | b[i + 3];
    i += 2 + len;
  }
  return null;
}

void main() {
  test('fridge icon = 350x350 baseline JPEG @ 72 DPI', () {
    // A non-square source with an alpha channel (worst case for the pipeline).
    final src = img.Image(width: 200, height: 120, numChannels: 4);
    img.fill(src, color: img.ColorRgba8(10, 120, 200, 255));
    img.fillCircle(src, x: 100, y: 60, radius: 30,
        color: img.ColorRgba8(240, 60, 30, 255));
    final srcPng = Uint8List.fromList(img.encodePng(src));

    final out = IconGeneratorService.toFridgeIcon(srcPng);
    expect(out, isNotNull);

    // 1) Decodes and is exactly 350x350.
    final decoded = img.decodeJpg(out!);
    expect(decoded, isNotNull);
    expect(decoded!.width, 350);
    expect(decoded.height, 350);

    // 2) Starts with a valid JPEG SOI.
    expect(out[0], 0xFF);
    expect(out[1], 0xD8);

    // 3) Baseline, NOT progressive.
    final sof = _sofMarker(out);
    expect(sof, isNotNull, reason: 'no SOF marker found');
    expect(sof, isNot(0xC2), reason: 'must not be progressive (FFC2)');
    expect(sof, 0xC0, reason: 'expected baseline SOF0');

    // 4) JFIF density = 72x72 dots-per-inch.
    expect(out[3], 0xE0, reason: 'APP0 marker');
    expect(out[13], 0x01, reason: 'density units = inch');
    expect((out[14] << 8) | out[15], 72, reason: 'Xdensity');
    expect((out[16] << 8) | out[17], 72, reason: 'Ydensity');
  });
}
