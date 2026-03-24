# Geometry Collection Auto-Creation System for EMFPhysicsProp

## Architecture Overview

The system has **two parts** that work together:

1. **Auto-Lookup (C++)** - `PostEditChangeProperty` on EMFPhysicsProp automatically finds and assigns `GC_{MeshName}` when you change PropMesh in the editor. Falls back to `FallbackGeometryCollection` if no matching GC exists.

2. **Batch Creator (C++ + EUW)** - `UGCBatchCreatorLibrary` provides Blueprint-callable functions for the `EUW_BatchGCCreator` widget to batch-create Voronoi-fractured GC assets from static meshes.

---

## How the Full Auto-Pipeline Works

### Editor: PostEditChangeProperty (when you set PropMesh)

When you change `PropMesh` on any `BP_EMFProp` instance or child blueprint in the editor:

1. `PostEditChangeProperty` fires
2. Gets the static mesh name (e.g., `SM_Crate_Wood`)
3. Searches for `GC_SM_Crate_Wood` in the **same Content Browser folder** as the mesh
4. **If found:** assigns it to `PropGeometryCollection`
5. **If NOT found:** auto-CREATES the GC via `UGCBatchCreatorLibrary::CreateGCFromStaticMesh()` (Voronoi fracture, auto piece count, saved next to the mesh)
6. **If creation fails:** uses `FallbackGeometryCollection`
7. Logs what happened to Output Log

This means **just setting PropMesh creates the GC asset automatically** — no manual batch step needed for individual props.

### Runtime: BeginPlay Fallback

If `PropGeometryCollection` is still null at runtime (e.g., prop was placed before the system existed, or GC was created after placement):

1. Searches for `GC_{MeshName}` in the same folder as the mesh asset
2. If found: assigns it (lightweight `LoadObject`, no creation at runtime)
3. If not found: uses `FallbackGeometryCollection`

This ensures **every EMFPhysicsProp always has a GC** without manual intervention.

**Setup required:** Set `FallbackGeometryCollection` in BP_EMFProp class defaults to your generic cube GC.

---

## EUW_BatchGCCreator - Blueprint Wiring Guide

Your EUW already has the UI layout. Here's how to wire each button to the C++ library:

### "Add Selected" Button
```
OnClicked:
  Call UGCBatchCreatorLibrary::GetSelectedStaticMeshes()
  For Each mesh in result:
    Add to your MeshList array variable (TArray<UStaticMesh*>)
    Add entry to ListView/ListBox widget
```

### "Clear List" Button
```
OnClicked:
  Clear MeshList array
  Clear ListView widget
```

### "Generate All" Button
```
OnClicked:
  Get PieceCount from your "Piece Count" SpinBox
  If "Auto Piece Count" checkbox is checked: set PieceCount = 0

  Set TotalMeshes = MeshList.Num()
  For Each (mesh, index) in MeshList:
    Update ProgressBar: (index / TotalMeshes)
    Call UGCBatchCreatorLibrary::CreateGCFromStaticMesh(mesh, PieceCount, false)
    Log result.Message

  Update ProgressBar: 1.0
  Show completion message with success/skip/fail counts
```

### "Auto Piece Count" Checkbox
```
When checked: disable the "Piece Count" SpinBox
When unchecked: enable the "Piece Count" SpinBox
The Generate All function passes PieceCount=0 when auto is checked.
```

### Auto Piece Count Reference
| Mesh Max Dimension | Piece Count |
|---|---|
| < 30 cm | 3 |
| 30-60 cm | 5 |
| 60-120 cm | 7 |
| 120-250 cm | 10 |
| 250-500 cm | 13 |
| > 500 cm | 16 |

---

## Naming Convention

| Source Mesh | Generated GC | Saved Location |
|---|---|---|
| `/Game/Props/SM_Crate_Wood` | `GC_SM_Crate_Wood` | `/Game/Props/GC_SM_Crate_Wood` |
| `/Game/Arena/SM_Barrel` | `GC_SM_Barrel` | `/Game/Arena/GC_SM_Barrel` |

Always saved in the **same folder** as the source mesh. No subfolder.

---

## How the Voronoi Fracture Works

The batch creator implements **triangle-centroid Voronoi partitioning**:

1. Generates N random 3D points (Voronoi sites) inside the mesh bounding box
2. For each triangle, computes its centroid
3. Assigns each triangle to the nearest Voronoi site
4. Groups triangles by assignment into "pieces"
5. Each piece gets its own vertex set (boundary vertices are duplicated)
6. Builds the GC with a root cluster transform + N rigid child transforms

**Fracture quality:** Cuts follow existing triangle edges rather than being perfectly planar. For destruction props that fly apart in 0.5 seconds during gameplay, this is visually indistinguishable from editor Voronoi. The deterministic seed (based on mesh name hash) ensures the same mesh always fractures the same way.

**GC hierarchy:**
```
Root (Clustered, no geometry)
  Piece_0 (Rigid, owns vertices 0..V0)
  Piece_1 (Rigid, owns vertices V0..V1)
  ...
  Piece_N (Rigid, owns vertices VN-1..VN)
```

The root cluster breaks immediately when `SpawnDestructionGC()` applies external strain fields.

---

## Complete Workflow: Adding a New Destructible Prop

### One-time setup:
1. Compile with the new C++ code (full recompile - header changes)
2. In BP_EMFProp class defaults, set `FallbackGeometryCollection` to your generic cube GC
3. Create EUW_BatchGCCreator widget (already done), wire buttons per guide above

### Per-prop workflow (fully automatic):
1. Place BP_EMFProp in level
2. Set PropMesh to your custom static mesh
3. **GC is auto-created and assigned instantly** (Voronoi fracture, saved next to mesh)
4. If creation somehow fails, fallback cube GC is used
5. Even at runtime, BeginPlay will find and assign the GC if it was missing

### Batch creating GCs for many meshes at once:
1. In Content Browser, select all the static meshes you want GCs for
2. Open EUW_BatchGCCreator
3. Click "Add Selected"
4. Check "Auto Piece Count" (or set manually)
5. Click "Generate All"
6. Watch progress bar
7. Done - all GC_{MeshName} assets created next to their source meshes

### When is the EUW batch tool still useful?
- Pre-generating GCs for **hundreds** of meshes at once (faster than placing props one by one)
- Re-generating GCs with a **specific piece count** (overriding auto)
- Regenerating GCs after mesh geometry changes (with bOverwriteExisting)

### Auto-pipeline means:
- **New prop placed, new mesh set** -> GC auto-created in editor
- **Existing prop, GC missing** -> GC found at BeginPlay (or fallback used)
- **Bulk import of new meshes** -> EUW batch tool pre-generates all GCs

---

## GC Settings You May Want to Tweak

After batch creation, the GCs have default settings. If you need to change settings for ALL GCs at once, modify the values in `BuildFracturedGC()` in `GCBatchCreatorLibrary.cpp`:

```cpp
// Line in BuildFracturedGC():
GCAsset->DamageThreshold = { 100.0f };  // Change this value
```

For per-instance settings, the EMFPhysicsProp's existing properties control runtime behavior:
- `DestructionImpulse` - how fast gibs scatter
- `DestructionAngularImpulse` - tumble speed
- `GibLifetime` - seconds before cleanup
- `GibCollisionProfile` - collision profile for pieces

---

## Files Modified/Created

| File | Change |
|---|---|
| `EMFPhysicsProp.h` | Added `PostEditChangeProperty`, `FallbackGeometryCollection` |
| `EMFPhysicsProp.cpp` | `PostEditChangeProperty` auto-create + BeginPlay runtime fallback |
| `GCBatchCreatorLibrary.h` | NEW - Blueprint function library header |
| `GCBatchCreatorLibrary.cpp` | NEW - Full Voronoi fracture + batch creation |
| `Polarity.Build.cs` | Added editor-only deps: UnrealEd, ContentBrowser |

**Full recompile required** (headers modified).
