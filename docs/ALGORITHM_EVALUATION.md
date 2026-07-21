# Algorithm Performance Evaluation

Smart Fridge — Group #8, Technion ICST

This document records **how** each algorithm in the project was evaluated and
**what quantitative results** were obtained. Every number below is either
measured and reproducible (method given) or explicitly marked as *not yet
measured*. Nothing here is estimated or assumed.

---

## 1. Algorithms in the project

| # | Algorithm | Where | Purpose |
|---|---|---|---|
| A1 | **Gemini vision food recognition** | `ESP32/SmartFridge_ESP32_CAM/gemini.h` | Photo → item names + quantity + confidence |
| A2 | **Multi-roof inventory merge** | `ESP32/SmartFridge_ESP32_CH/inventory_merge.h` | Merge N per-camera docs into one inventory |
| A3 | **Icon normalization** | `flutter_app/lib/services/icon_generator_service.dart` | Any image → 350×350 baseline JPEG @72 DPI |
| A4 | **Barcode → product resolution** | `ESP32/SmartFridge_ESP32_CH/gm65.h` | EAN → product name (Open Food Facts) |
| A5 | **Expiry classification** | `flutter_app/lib/models/fridge_item.dart` | Dates → expired / critical / soon / ok |
| A6 | **Settings two-way sync** | `settings_sync.h` ↔ `fridge_settings_service.dart` | Screen ↔ app convergence |

---

## 2. Measured results

### 2.1 A1 — Vision recognition, field statistics

**Method.** Every scan is persisted to Firestore
(`fridges/fridge1/scans/{timestamp}`) with the items the model returned. We
queried the full history and aggregated it — i.e. this is *observational field
data from real fridge usage*, not a lab run.

**Conditions.** 148 scan documents, **2026-05-03 → 2026-07-21** (~11 weeks),
2× ESP32-CAM (AI-Thinker, roof1/roof2) + earlier single-camera builds, WS2811
strip on during capture (roof1), model `gemini-2.0-flash`, prompt in
`gemini.h`. Records with a pre-2000 timestamp (unsynced NTP clock) excluded.

**Results.**

| Metric | Value |
|---|---|
| Scan documents analysed | **148** |
| Total item detections | **192** |
| Items per scan | mean **1.30**, median 1, min 0, max 5 |
| Scans returning **zero** items | **21 / 148 = 14.2 %** |
| Distinct item names ever produced | **69** |
| Source split | ESP32-CAM 92, roof1 26, roof2 30 |

Model **self-reported** confidence over all 192 detections:

| Confidence | Count | Share |
|---|---|---|
| high | 71 | **37.0 %** |
| medium | 54 | 28.1 % |
| low | 66 | **34.4 %** |
| medium-high (off-schema) | 1 | 0.5 % |

> ⚠️ Self-reported confidence is **not** accuracy — it is the model grading
> itself. True accuracy requires ground truth (§3.1). It is reported here
> because it is what the system actually stores and acts on.

**Findings.**

1. **Naming instability is the dominant failure mode.** 69 distinct names were
   produced for a fridge that physically held on the order of 10–15 distinct
   products. Observed variants of the same product include
   `water` / `reusable water bottle`, and
   `soft drink` / `soft drink - sugar free`. This directly motivated the
   canonical-name matching in the reconciliation design
   ([INVENTORY_RECONCILIATION_PLAN.md](INVENTORY_RECONCILIATION_PLAN.md)).
2. **14.2 % of scans detect nothing at all**, so a door-close event fairly
   often yields no inventory signal.
3. Only **37 %** of detections are high-confidence; roughly one third are
   explicitly low-confidence, which is why low-confidence detections are not
   allowed to create new inventory rows.
4. The model occasionally violates the prompt's output schema
   (`medium-high`), so the app normalizes confidence defensively.

### 2.2 A3 — Icon normalization

**Method.** Reproducible micro-benchmark committed as
`flutter_app/test/icon_encode_benchmark_test.dart`; run with
`flutter test test/icon_encode_benchmark_test.dart`.

**Conditions.** Apple Silicon macOS host, Dart VM, synthetic 1024×1024 RGB
source (same resolution the image model returns), 5 runs, `package:image`
JPEG encoder at quality 90.

| Metric | Value |
|---|---|
| Runs | 5 |
| Mean | **40.0 ms** |
| Median / min / max | 35 ms / 32 ms / 57 ms |
| Output | 350×350, baseline JPEG, 72×72 DPI |

**Correctness** is separately asserted by `test/icon_spec_test.dart`, which
verifies on the encoded bytes: exact 350×350 dimensions, JPEG SOI, **SOF0
baseline** marker (never SOF2 progressive), and JFIF density 72×72 inch — the
format the ESP32 TFT decoder requires. Both tests pass.

### 2.3 Icon generation latency (network)

**Method.** 3 sequential HTTPS calls to `gemini-2.5-flash-image`
(`:generateContent`, `responseModalities:[TEXT,IMAGE]`) with the production
icon prompt, timed with `/usr/bin/time`.

**Conditions.** Developer machine, home broadband, 2026-07-21.

| Run | Latency | Returned image |
|---|---|---|
| 1 | 4.51 s | 228 898 B |
| 2 | 5.93 s | 208 838 B |
| 3 | 4.77 s | 226 658 B |
| **Mean** | **≈ 5.07 s** | ≈ 221 KB (1024×1024 PNG) |

So end-to-end cost of publishing one new icon ≈ **5.1 s network + 40 ms
encode**; the encode is ~0.8 % of the total, i.e. generation is entirely
network/model bound.

### 2.4 A6 — Settings sync convergence

**Method.** Live end-to-end test against the production Firebase project, app
running on an Android emulator.

1. *App → fridge*: tapped a stepper in the app's Fridge Settings screen, then
   read `fridges/fridge1/settings` over the RTDB REST API.
2. *Fridge → app*: wrote the node directly with `updated_by:"screen"`
   (simulating the ESP32 screen) and observed the app UI.

**Results.**

| Direction | Result |
|---|---|
| App → RTDB | `temp_min` 4 → 5 with `updated_by:"app"` — correct tag for the firmware's echo guard ✅ |
| RTDB → App | `temp_min:2`, `expiry_warn_days:5` appeared in the UI **without any refresh**, observed within the ~4 s sampling interval ✅ |
| RTDB write latency | 0.43 s, 0.52 s (n = 2) |

> Network latency samples are small (n = 2–3) and taken from a developer
> machine; treat them as indicative, not as a characterised distribution. One
> read sample was discarded as a clear outlier (75 s) caused by the measuring
> environment, not the system.

Firmware-side convergence bound: the CH board polls RTDB every
`SETSYNC_POLL_MS = 5000` ms (`settings_sync.h`), so an app-side change is
applied on the fridge screen within **≤ 5 s** by construction.

---

## 3. Not yet measured — required experiments

These need ground truth or hardware instrumentation and are **not** reported
above because we do not have the data. Protocols are given so the numbers can
be produced.

### 3.1 A1 — Recognition accuracy (the key missing metric)

Field data cannot yield accuracy: nothing records what was *actually* in the
fridge. Proposed controlled experiment:

**Setup.** Fix a set of N ≥ 15 known products. For each trial, place a known
subset in the fridge, record the ground-truth list, close the door to trigger
a scan, then read the resulting `scans/{ts}` document.

**Vary (report each condition separately):** number of items (1 / 3 / 5 / 8),
occlusion (items fully visible vs partially hidden), LED strip on vs off,
and shelf (roof1 vs roof2).

**Compute**, per condition, matching detected↔truth by canonical name:

| Symbol | Meaning |
|---|---|
| TP | item present **and** detected |
| FP | detected but not present (hallucination) |
| FN | present but missed |

- **Precision** = TP / (TP + FP)
- **Recall** = TP / (TP + FN)
- **F1** = 2·P·R / (P + R)
- **Name accuracy** = fraction of TPs given the canonical name
- **Quantity accuracy** = fraction of TPs with correct count
- **Confidence calibration**: precision computed *within* each self-reported
  confidence bucket — this is what validates or refutes §2.1's confidence data

Suggested ≥ 10 trials per condition (≥ 160 scans total) for meaningful rates.

| Condition | Trials | TP | FP | FN | Precision | Recall | F1 |
|---|---|---|---|---|---|---|---|
| 1 item, clear, LED on | | | | | | | |
| 3 items, clear, LED on | | | | | | | |
| 5 items, clear, LED on | | | | | | | |
| 5 items, occluded | | | | | | | |
| 5 items, LED off | | | | | | | |

### 3.2 A1 — Scan latency (door close → inventory updated)

Instrument with `millis()` on the CAM board across: capture → base64 → Gemini
round trip → Firestore write. Report mean/median/p95 over ≥ 20 scans. The
serial log already prints stage markers; only timestamps need adding.

### 3.3 A2 — Merge correctness

Exercise the scenarios in the reconciliation test matrix (item removed, item
moved between shelves, one roof board offline, barcode item never seen by a
camera) and report pass/fail plus removal latency in door-cycles.

### 3.4 A4 — Barcode resolution rate

Scan ≥ 30 real products; report the share resolved to a name by Open Food
Facts, and median lookup latency. (Known: OFF's *search* endpoints were
observed returning 502/503, while the `/api/v2/product/{barcode}` endpoint
answered reliably in ad-hoc checks — quantify this.)

---

## 4. Reproducing the measurements

```bash
# A3 correctness + benchmark
cd flutter_app
flutter test test/icon_spec_test.dart
flutter test test/icon_encode_benchmark_test.dart

# A1 field statistics — aggregate the scan history
#   query fridges/{id}/scans (timestamp >= 2000-01-01) and aggregate
#   confidence / items-per-scan / distinct names, as in §2.1
```

Raw source of §2.1 is the live `fridges/fridge1/scans` collection; re-running
the aggregation on a later date will yield larger N.

---

*Last updated: 2026-07-21. Sections 2.x are measured; section 3 is outstanding
work.*
