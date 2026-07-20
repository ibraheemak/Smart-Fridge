# Camera + Barcode Inventory Reconciliation Plan

## Context

The fridge has two independent inventory sources that currently fight each other:
- **2× ESP32-CAM AI scans** (one per shelf) — each scan fully replaces its `inventory/roof{N}` doc
- **GM65 barcode scanner** on the CH board — increments/appends items in `inventory/current`

Verified problems in the current code (explored `inventory_merge.h`, `gm65.h`, `touch.h`, CAM `firebase.h`):
1. **Nothing is ever auto-removed.** The merge carries over every `/current` item absent from roof scans (added to protect barcode items — but it protects everything). Removing food never removes it from inventory.
2. **Barcode + camera duplicate rows.** Open Food Facts names ("Tnuva Milk 3%") never `equalsIgnoreCase`-match Gemini labels ("milk") — the same physical product becomes two rows.
3. **Every roof scan is a full replace** — an item the AI misses once vanishes from that roof's evidence.
4. **Lost-update races:** merge, GM65, and the touch UI all do read-modify-write full-doc PATCHes to `/current`; the touch UI writes from stale memory with no re-fetch.
5. **No provenance:** a barcode item is only detectable via the `confidence=="100"` hack.
6. Expiries grow with quantity but never shrink; stale roof docs (offline board) can double-count.

## User decisions (fixed)
- **Keep-unseen model**: items missed by cameras are kept and flagged; blind spots (door shelves/drawers) are real.
- Scanner is **IN-only**.
- Scope: **firmware + app** (app surfaces provenance + unseen state, and gets manual remove).

## Design

### New per-item schema in `inventory/current` (fields omitted at default → back-compat + doc-size bound)
- `origin`: "camera" (default) | "barcode"
- `everSeen` (bool, default true): ever camera-confirmed. Barcode items start `false`.
- `missedScans` (int, default 0): consecutive new-evidence merges without camera confirmation
- `lastSeenRoof` (int), `barcode` (string, barcode items), `alias` (raw OFF name when it differs from canonical)
- `qtyPendingDown` (int): decrease-debounce holding slot
- Doc-level `roofProcessed` map: last consumed `updatedAt` per roof (dedupes the double-merge per door cycle)
- Legacy sniff: existing items with all-digit confidence → treated as `origin=barcode, everSeen=false`. No migration pass needed.

### Core rules
1. **Removal** (finally works): camera-seen (`everSeen`) item missing from a merge with *new evidence from its roof* → `missedScans++`; at `MISS_REMOVE_THRESHOLD=2` → removed. Any sighting resets to 0. Items with `everSeen=false` (blind-spot barcode adds) are **never auto-removed** — manual only.
2. **New-evidence gating**: a roof's doc only counts (for presence, absence, or miss-counting) if it's fresh (`updatedAt` within `ROOF_STALE_WINDOW_S=180` of the newest roof) AND newer than `roofProcessed[roof]`. Stale/offline roofs are "no evidence" — no double-counting, no false misses, no false removals.
3. **Name reconciliation** (kills duplicates): deterministic `canonicalize()` on the CH board against the existing `basic-items` list (lowercase + whole-word containment, longest match wins). GM65 canonicalizes at insert time and stores the raw OFF name as `alias`; merge matches camera detections against name AND alias. Match order for barcode re-scans: barcode → canonical name → alias. **No Gemini call on CH** (CH has no Gemini plumbing today; deterministic matching suffices, seam left for later).
4. **Quantity**: camera count wins for `everSeen` items; increases apply immediately (feeds existing expiry-prompt flow); decreases need **2 consecutive scans agreeing** (`qtyPendingDown`) since they destroy expiry data. Blind-spot item counts stand as scanned.
5. **Expiry shrink**: on confirmed decrease, remove empty slots first, then soonest dates (FIFO assumption).
6. **Low-confidence guard**: low-conf detections can refresh existing items but cannot create new rows.
7. **Races**: CH stays the **sole writer** of `/current`. Touch UI write becomes rebase-style (fresh GET → mutate only the expiry slot in the parsed doc → PATCH). The app never writes `/current`; it writes op docs to new `inventory_ops` collection (`remove` / `setQuantity`) + bumps the RTDB doorbell; CH applies ops before merging.
8. **Buffers**: all `/current` parsers go 8192 → `INV_JSON_CAPACITY=16384` (today an oversized doc silently fails to parse and GM65 would then **wipe the inventory** by writing a 1-item doc). Do this first.

### Merge algorithm (Phase A–F)
A: GET roof docs, compute fresh/newEvidence per roof. B: GET `/current` + `roofProcessed`. C: union fresh-roof items by canonical name (sum qty, max confidence, roofMask). D: reconcile each existing item — matched: reset miss state, one-time canonical rename (old name → alias), quantity rules; unmatched: keep verbatim if `everSeen=false`, else miss-count/remove per rule 1. E: append brand-new camera items (skip low-conf). F: update `roofProcessed`, full-doc PATCH (no doorbell bump — merge is the doorbell consumer).

## File-by-file

**CH firmware** (`ESP32/SmartFridge_ESP32_CH/`)
- `parameters.h`: `MISS_REMOVE_THRESHOLD 2`, `ROOF_STALE_WINDOW_S 180`, `INV_JSON_CAPACITY 16384`, `MAX_OPS_PER_CYCLE 5`
- `canonical.h` (new): `fetchBasicItemsCached` (6h TTL, port of CAM `firebase.h:49-66`), `canonicalize`, `wordContains`, `parseTs`
- `inventory_merge.h`: full rewrite per Phase A–F
- `gm65.h`: pass barcode into `addScannedItemToInventory`; canonicalize + alias; match barcode→name→alias; new items get `origin=barcode, everSeen=false, confidence="high"`; mutate parsed doc in place (preserve unknown fields)
- `touch.h`: `persistItemsToFirestore` → rebase-style `persistExpiry(name, slot, value)`; add "Remove" button to detail view
- `inventory_ops.h` (new): `processInventoryOps()` — list `inventory_ops` docs, apply ≤5 per cycle to fresh `/current`, delete processed ops; called on doorbell + boot, before merge
- `SmartFridge_ESP32_CH.ino`: buffer bump in `fetchInventory`, parse new fields into `InventoryItem`, clamp `expiry_count` when quantity shrank, call `processInventoryOps`
- `display.h`: `InventoryItem` gains `origin`/`unseen`; row renders unseen marker

**CAM firmware**: no behavioral change (roof docs stay full-replace snapshots — that's correct). Only check/remove uncalled `fetchExistingExpiries`.

**Flutter app** (`flutter_app/lib/`)
- `models/fridge_item.dart`: parse `origin`/`everSeen`/`missedScans`/`barcode`/`alias`; `isUnseen`, `isBarcodeOrigin` getters
- `services/fridge_service.dart`: `submitInventoryOp(action, name, {quantity})` → `inventory_ops` doc + RTDB doorbell bump (reuse liveview RTDB pattern)
- `screens/inventory_screen.dart`: unseen badge + provenance icon per card; item bottom-sheet with quantity stepper + "Remove from fridge"
- Note: Firestore rules must allow the app to create/read `inventory_ops` (rules live outside repo — user action).

## Implementation order
1. Buffer bumps + `canonical.h` (fixes latent inventory-wipe cliff; standalone)
2. `touch.h` rebase write (kills race; standalone)
3. New merge + constants (core; degrades gracefully on legacy docs)
4. `gm65.h` changes
5. `inventory_ops` on CH + touch Remove button
6. Flutter model/UI/`submitInventoryOp`

## Test matrix (each = one physical scenario, pass criteria in `/current` + app)
1. Remove camera item → unseen after cycle 1, gone after cycle 2
2. Barcode then camera-recognized → single canonical row with alias+barcode, `everSeen` flips true
3. Barcode in drawer → persists forever; removable via app op (CH applies within seconds)
4. Shelf move in one door cycle (two merges) → qty unchanged, `lastSeenRoof` flips, no miss
5. Roof board offline 3 cycles → its items frozen, no removal/double-count
6. Name jitter ("milk"/"milk carton") → one canonical row
7. Count jitter 3→2→3 → stays 3, no expiry lost; real 3→2→2 → one expiry removed (empty slot first)
8. GM65 scan while expiry editor open → both changes survive (rebase)
9. Reboot with new fields → prompts only for empty slots; fields survive Save/Skip
10. ~25 items with expiries → no `deserializeJson` NoMemory on any path

## Accepted limitations (documented, not bugs)
- Camera-count-wins can erase a blind-spot *unit* of a camera-seen item (needs per-unit location tracking — out of scope)
- A seen item moved INTO a blind spot is removed after K cycles (inherent to eviction-by-non-observation; unseen badge gives 1-cycle warning; K is one constant)
- Removal latency = 2 full door cycles by design (immunity to double-merge and roof staleness)
