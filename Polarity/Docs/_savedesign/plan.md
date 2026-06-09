I'll synthesize the design now. The judges split (C wins two lenses on correctness + maintainability; A wins extensibility), so I'll recommend the two-tier explicit-fields design (C) as the spine and graft the best ideas from the runners-up.

# Polarity Save System вЂ” Recommended Design

## Recommended approach (+ rationale and what was grafted)

**Adopt Design C вЂ” Two-tier domain saves: two physical `USaveGame` slots (`Polarity_Meta`, `Polarity_Run`), each its own subclass with explicit, typed `UPROPERTY` fields, orchestrated by one `USaveGameSubsystem` (a `UGameInstanceSubsystem`).**

Rationale: two of three judges (Correctness/Lifecycle and Simplicity/Debuggability) picked C, and the third (Extensibility) ranked it solidly mid-pack вЂ” meaning C is the choice that is never *wrong* on any axis. It wins because explicit typed fields route through Unreal's native, battle-tested tagged-property serialization (no proxy-archive edge cases for the meta layer that will evolve the most), every persisted value is debugger- and SaveGame-viewer-inspectable (critical for "the raid icon didn't appear after restart вЂ” why?"), and two physical slots make the meta/run firewall a filesystem fact rather than a convention вЂ” a corrupt high-frequency run checkpoint physically cannot brick permanent progression, and `DeleteGameInSlot` is the atomic "abandon run." It scatters into subsystems through their public API, which is the only scatter mechanism that doesn't fight `InitializeDependency` ordering.

Grafted from runners-up:
- **From A (winner of the extensibility lens):** the explicit **idempotency contract** on `NotifyTutorialCompleted()` and a hard **`bMetaLoaded` guard** that forbids any auto-save before the initial load completes (prevents the nastiest silent bug вЂ” a subsystem racing the loader and overwriting the file with defaults). Also A's documented **single-in-flight save/load guard** invariant.
- **From B:** the explicit recommendation of a **gatherв†’scatter round-trip unit test** as a CI gate вЂ” converts C's worst failure mode (forget a copy line в†’ silent non-persistence) from silent to a red build. Also B's framing that **the persisted `uint8` byte is the contract; the C++ `ERunState` enum is not**.
- **From D:** **per-payload CRC32 computed over the exact bytes** `LoadGameFromSlot` returns (deterministic вЂ” avoids the `TMap`-iteration-order false-corruption trap A's container-CRC walks into), and the **"a missing record/field is never an error в†’ defaults"** load posture for forward/back compatibility.

The future ability bank is stored as `TArray<FAbilityBankEntry>{FGameplayTag, int32 Level}` (a struct array, additively extensible) rather than a raw `TMap<FGameplayTag,int32>` (retyping a map value silently resets it) вЂ” also grafted from C's own draft, hardened per Judge 1.

---

## Architecture & design doc

### Meta vs Run split

Rule: *if losing the current run should erase it в†’ Run-tier; if it must make run N+1 differ from run N в†’ Meta-tier.*

**Meta slot `Polarity_Meta`** (durable; loaded once on boot; written debounced-async on meaningful meta change, sync on quit; never auto-deleted):

| Field | Owner today | Source | Stored as |
|---|---|---|---|
| `MetaCurrency` | `UStreamSubsystem` | `StreamSubsystem.h:202-203` | `int64` |
| `CompletedRuns` | `UStreamSubsystem` | `:214-215` | `int32` |
| `PlayerStreamerName` | `UStreamSubsystem` | `:206-207` | `FString` |
| `ConsumedLoreIDs` | `ULoreSubsystem` | `LoreSubsystem.h:91-92` | `TSet<FName>` |
| **`bTutorialCompleted`** | **save object (new, req #1)** | вЂ” | `bool` |
| **`UnlockedApps`** | **save object (new, req #2)** | вЂ” | `TSet<FName>` |
| **`Chat` read-state** | **save object (new, req #3)** | вЂ” | `FChatReadState` |
| **Ability bank / unlock pool / cosmetics / starting boosts** | **save object (new, req #4 reserved)** | вЂ” | `TArray<FAbilityBankEntry>` + `TSet<FGameplayTag>` + `TSet<FName>` + `TMap<FName,int32>` |

**Run slot `Polarity_Run`** (volatile; created on `StartRun`, overwritten per-arena, **deleted** on death/victory, **kept** on quit-to-menu = resume signal):

| Field | Owner | Source | Stored as |
|---|---|---|---|
| `RunState` | `URunSubsystem` | `RunSubsystem.h:158-159` | `uint8` (byte is the contract, not the enum) |
| `CurrentArenaIndex` | `URunSubsystem` | `:161-162` | `int32` |
| `Stats` (`FRunStats`) | `URunSubsystem` | `:164-165` | flattened scalars + `KillsByEnemyClassPath` |
| `ActivatedAntennaCount` | `URunSubsystem` | `:168-169` | `int32` |
| `AcquiredUpgrades` | `URunSubsystem` | `:173-174` | `TMap<FGameplayTag,int32>` |
| `Progress` (`FSkillState`) | `UXPSubsystem` | `XPSubsystem.h:107-108` | `int32 XP_CurrentXP; int32 XP_CurrentLevel` |

**Explicitly NOT saved:** `UTutorialSubsystem::CompletedTutorials` (non-`UPROPERTY`, session-only by design, `TutorialSubsystem.h:345` вЂ” persisted truth is `bTutorialCompleted`); `UStreamSubsystem::bCurrentRunMilestoneReached` (`Transient`); `UDestroyedIslandsSubsystem::DestroyedIslandIDs` (session-scoped by design); `UShooterSettingsSubsystem` (already persists via `UGameUserSettings`/config вЂ” separate channel). Ability-gate fields on `APolarityCharacter`/`UMeleeAttackComponent` are **runtime mirrors** re-applied on `BeginPlay` from the ledger (run) or ability bank (meta) вЂ” never persisted from the transient actor (double-source-of-truth hazard, INTEL #1).

> **Reuse of existing `UPROPERTY(SaveGame)` specifiers:** under this explicit-fields design the `SaveGame` specifiers on the subsystems become **inert** (we copy through getters/setters, not via reflection-walking the subsystem). They are left in place (harmless) but **not relied upon**. No subsystem field is re-typed or renamed.

### Exact serialization method

- **Primary path:** explicit typed `UPROPERTY` on each `USaveGame` в†’ native engine tagged-property serialization via `UGameplayStatics::CreateSaveGameObject` / `SaveGameToSlot` / `AsyncSaveGameToSlot` / `LoadGameFromSlot`. No `FObjectAndNameAsStringProxyArchive` in the hot path.
- **The one fragile type** вЂ” `FRunStats::KillsByEnemy` is `TMap<TSubclassOf<AShooterNPC>,int32>` (`RunSubsystem.h:47`). `TSubclassOf` keys do not round-trip cleanly. **Flatten** on gather to `TMap<FString,int32> KillsByEnemyClassPath` (key = `FSoftObjectPath(Class).ToString()`); **resolve** on scatter via `TSoftClassPtr<AShooterNPC>(FSoftObjectPath(Key)).LoadSynchronous()`, dropping keys that no longer resolve (run-scoped в†’ acceptable loss, logged).
- **Enums:** `ERunState` stored as `uint8` by value вЂ” append enumerators only, never reorder `None/Active/Paused/Ended`.
- **Tags:** validate every `FGameplayTag` key on load via `UGameplayTagsManager::Get().RequestGameplayTag(Name, /*ErrorIfNotFound*/false)`; drop invalid.

### Versioning & migration

- Two independent `int32 SaveVersion` fields (one per save class) + sequential chain migration on load. Chosen over `FCustomVersion` GUID because both are flat `USaveGame`s with reflected fields, not hand-rolled `Serialize()` streams.
- Rules: adding a `UPROPERTY` is additive-safe (old saves default it вЂ” zero migration for reserved req #4 fields); rename/retype silently resets (add a migration step or `meta=(DeprecatedProperty)` shadow); forward-version saves (`SaveVersion > Current`) are **quarantined** (renamed `.future`), never downgraded.

### Corruption-safe write

- **Atomic tempв†’promote:** async-write to `<Slot>_tmp`; on `bSuccess` rotate liveв†’`.bak` then `IFileManager::Move(tmpв†’live)` (atomic rename on one volume).
- **Validate on load:** recompute CRC32 (`FCrc::MemCrc32` over the exact loaded bytes, grafted from D) and compare to stored `PayloadChecksum`; on mismatch fall back to `.bak`; on double-fail start fresh + log loudly. Range-sanity post-load (`MetaCurrency >= 0`, `CurrentArenaIndex в€€ [0,NumArenas)`).
- **Run slot is expendable:** any failed check в†’ discard + offer fresh run, never block. Meta slot gets full backup+validate.

### Lifecycle

- **Load on boot:** `USaveGameSubsystem::Initialize` loads `Polarity_Meta` **synchronously** (subsystems init before first map's widgets, so the desktop's first frame reads correct state вЂ” avoids the raid-icon first-frame flicker, INTEL #2). Sets `bMetaLoaded=true` only after a successful load/create.
- **Ordering:** each meta-bearing subsystem calls `Collection.InitializeDependency(USaveGameSubsystem::StaticClass())` (matches `StreamSubsystem.cpp:27`, `XPSubsystem.cpp:24`). Scatter is via public setters so it tolerates a subsystem that initializes after the save subsystem (re-pullable).
- **Save triggers:** meta mutations (`AddMetaCurrency`, `MarkLoreConsumed`, `NotifyTutorialCompleted`, chat read changes) в†’ `RequestMetaSave()` (dirty flag + ~5s debounce timer). High-value transitions (`EndRun`, tutorial-complete, return-to-menu, app-deactivate via `FCoreDelegates::ApplicationWillDeactivateDelegate`) в†’ `SaveMetaNow()` (immediate sync). Run: `StartRun`/`EnterArena`/`ClearArena` в†’ `SaveRun()`; `EndRun` в†’ bank rewards в†’ `SaveMetaNow()` в†’ `ClearRun()` (death/victory) or final `SaveRun()` + keep (quit). **Single in-flight guard** serializes all save/load ops (some platforms can't do both at once, grafted from A).
- **PIE isolation:** `#if WITH_EDITOR` slot-name suffix so editor testing never stomps a packaged profile.

### Blueprint-facing API (full surface)

All on `USaveGameSubsystem`, reached via the stock *Get Game Instance Subsystem* node. See code draft below for exact signatures: tutorial (`NotifyTutorialCompleted`/`IsTutorialCompleted`), desktop (`IsAppUnlocked`/`UnlockApp`/`GetUnlockedApps`), chat (`ShouldPlayChatRealtime`/`GetUnreadChatCount`/`HasUnreadChat`/`GetUnlockedChatGroups`/`UnlockChatGroup`/`MarkChatGroupSeen`/`MarkChatRead`), run resume (`HasRunSave`/`GetResumeArenaIndex`/`ResumeRun`/`SaveRun`/`ClearRun`), lifecycle (`SaveMetaNow`/`RequestMetaSave`/`WipeMeta`), and the `OnDesktopStateChanged`/`OnSaveLoaded` notify delegates.

---

## File manifest

| Path | New/Modify | Header/Cpp/Both | Purpose / Rebuild note |
|---|---|---|---|
| `Polarity/Save/PolaritySaveTypes.h` | New | Header | `EPolaritySaveScope`, `EPolaritySaveVersion`, `FChatReadState`, `FAbilityBankEntry`, CRC helper decl. **New header в†’ part of one-time rebuild.** |
| `Polarity/Save/PolarityMetaSave.h/.cpp` | New | Both | `UPolarityMetaSave : USaveGame` вЂ” explicit meta fields. **New header в†’ rebuild.** |
| `Polarity/Save/PolarityRunSave.h/.cpp` | New | Both | `UPolarityRunSave : USaveGame` вЂ” explicit run fields. **New header в†’ rebuild.** |
| `Polarity/Save/SaveGameSubsystem.h/.cpp` | New | Both | Orchestrator: load/save, gather/scatter, versioning, atomic write, debounce, BP API. **New header в†’ rebuild.** |
| `Polarity/Variant_Shooter/Run/RunSubsystem.h/.cpp` | Modify | Both (h touch = rebuild) | Add `WriteToRun/ReadFromRun` accessor decls (or reuse public getters); add `SaveRun()` hooks in `StartRun/EnterArena/ClearArena/EndRun` (cpp). **Header touch в†’ rebuild.** Existing `SaveGame` fields **reused as-is** (read via getters, not reflection). |
| `Polarity/Variant_Shooter/Stream/StreamSubsystem.h/.cpp` | Modify | Cpp-only if existing getters suffice | Hook `OnMetaCurrencyChanged`/`MarkRunMilestoneReached` в†’ `RequestMetaSave` (cpp). `InitializeDependency(USaveGameSubsystem)` (cpp). **No header change needed** вЂ” uses existing `GetMetaCurrency/AddMetaCurrency/GetCompletedRuns/GetPlayerStreamerName`. |
| `Polarity/Variant_Shooter/XP/XPSubsystem.h/.cpp` | Modify | Cpp-only if getters suffice | `InitializeDependency` + scatter via existing API (cpp). Header touch only if XP needs new private-field accessors. |
| `Polarity/Variant_Shooter/Lore/LoreSubsystem.h/.cpp` | Modify | Cpp-only if getters suffice | Hook `MarkLoreConsumed` в†’ `RequestMetaSave`; scatter via `GetConsumedLoreIDs`/consume API (cpp). |
| `Polarity/Save/SaveDebugCommands.cpp` (optional) | New | Cpp | `polarity.save.wipe`, `polarity.save.dumpmeta` console cmds (cpp-only). |

> Tutorial level BP, Desktop UMG, Chat UMG are **content**, not source вЂ” they call the BP API; no `.cpp`/`.h` edits.

---

## C++ code drafts

### `Polarity/Save/PolaritySaveTypes.h`

```cpp
#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "PolaritySaveTypes.generated.h"

/** Which physical slot a piece of state belongs to. */
UENUM()
enum class EPolaritySaveScope : uint8
{
    Meta,   // Polarity_Meta вЂ” durable profile
    Run     // Polarity_Run  вЂ” volatile mid-run resume
};

/** Schema versions. Bump on every NON-additive schema change; chain-migrate on load. */
namespace PolaritySaveVersion
{
    inline constexpr int32 MetaInitial = 1;
    inline constexpr int32 MetaLatest  = MetaInitial; // == 1 today

    inline constexpr int32 RunInitial  = 1;
    inline constexpr int32 RunLatest   = RunInitial;  // == 1 today
}

/** Chat read-state, grouped so the meta header stays readable. */
USTRUCT()
struct FChatReadState
{
    GENERATED_BODY()

    // Which message GROUPS are visible. FName-keyed (rename-tolerant): "intro", "post_tutorial".
    UPROPERTY()
    TSet<FName> UnlockedGroups;

    // Groups whose REAL-TIME playback already happened once. In set => later visits dump instantly.
    UPROPERTY()
    TSet<FName> SeenGroups;

    // Read high-water mark over the CSV '---' row index. -1 == nothing read yet.
    // Unread = (max unlocked row index) - LastReadMessageIndex.
    UPROPERTY()
    int32 LastReadMessageIndex = -1;
};

/**
 * Future meta-layer ability entry (req #4 headroom).
 * Struct (not raw TMap value) so it extends additively: add cost/prereq/unlock-source
 * later by appending UPROPERTYs without re-typing an existing map value (which would reset it).
 */
USTRUCT()
struct FAbilityBankEntry
{
    GENERATED_BODY()

    UPROPERTY()
    FGameplayTag AbilityTag;

    UPROPERTY()
    int32 Level = 1; // L1..L3
};
```

### `Polarity/Save/PolarityMetaSave.h`

```cpp
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "GameplayTagContainer.h"
#include "PolaritySaveTypes.h"
#include "PolarityMetaSave.generated.h"

/** Durable profile. Slot "Polarity_Meta". Every field is an explicit, inspectable UPROPERTY. */
UCLASS()
class POLARITY_API UPolarityMetaSave : public USaveGame
{
    GENERATED_BODY()

public:
    // ---- Schema / integrity header ----
    UPROPERTY()
    int32 SaveVersion = PolaritySaveVersion::MetaInitial;

    /** CRC32 over the exact serialized payload (recomputed on save, verified on load). */
    UPROPERTY()
    uint32 PayloadChecksum = 0;

    UPROPERTY()
    FDateTime SavedAtUtc = FDateTime(0);

    // ---- UStreamSubsystem meta ----
    UPROPERTY()
    int64 MetaCurrency = 0;

    UPROPERTY()
    int32 CompletedRuns = 0;

    /** Default chosen by the user (see Open Decisions). Empty here = "use code default at apply time". */
    UPROPERTY()
    FString PlayerStreamerName;

    // ---- ULoreSubsystem ----
    UPROPERTY()
    TSet<FName> ConsumedLoreIDs;

    // ---- Tutorial + desktop (req #1, #2) вЂ” owned HERE, set by the tutorial LEVEL event ----
    UPROPERTY()
    bool bTutorialCompleted = false;

    UPROPERTY()
    TSet<FName> UnlockedApps; // e.g. "raid", "training", "chat", "settings"

    // ---- Chat read-state (req #3) ----
    UPROPERTY()
    FChatReadState Chat;

    // ---- Future meta layer (req #4) вЂ” reserved homes, empty today, populating later is .cpp-only ----
    UPROPERTY()
    TArray<FAbilityBankEntry> AbilityBank;        // ability -> L1..L3

    UPROPERTY()
    TSet<FGameplayTag> UnlockedUpgrades;          // pool additions (stable tag keys)

    UPROPERTY()
    TSet<FName> UnlockedCosmetics;

    UPROPERTY()
    TMap<FName, int32> StartingSkillBoosts;
};
```

### `Polarity/Save/PolarityRunSave.h`

```cpp
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "GameplayTagContainer.h"
#include "PolaritySaveTypes.h"
#include "PolarityRunSave.generated.h"

/** Volatile mid-run resume. Slot "Polarity_Run". Deleted on death/victory; kept on quit-to-menu. */
UCLASS()
class POLARITY_API UPolarityRunSave : public USaveGame
{
    GENERATED_BODY()

public:
    UPROPERTY()
    int32 SaveVersion = PolaritySaveVersion::RunInitial;

    UPROPERTY()
    uint32 PayloadChecksum = 0;

    // ---- URunSubsystem run-tier (mirrors RunSubsystem.h:158-174) ----
    // NOTE: the uint8 is the persisted contract; the C++ ERunState enum is NOT. Append-only.
    UPROPERTY()
    uint8 RunState = 0;

    UPROPERTY()
    int32 CurrentArenaIndex = -1;

    UPROPERTY()
    int32 ActivatedAntennaCount = 0;

    UPROPERTY()
    TMap<FGameplayTag, int32> AcquiredUpgrades; // tag keys = rename-tolerant

    // ---- FRunStats flattened (RunSubsystem.h:32-48) ----
    UPROPERTY()
    int32 TotalXPEarned = 0;

    UPROPERTY()
    int32 LevelsGained = 0;

    UPROPERTY()
    float RunDuration = 0.f;

    // KillsByEnemy is TMap<TSubclassOf<AShooterNPC>,int32>: TSubclassOf keys do NOT round-trip
    // cleanly. Flatten to soft-class-path string keys; resolve at a controlled point on load.
    UPROPERTY()
    TMap<FString, int32> KillsByEnemyClassPath;

    // ---- UXPSubsystem::Progress (FSkillState; SkillTypes.h:24-34) ----
    UPROPERTY()
    int32 XP_CurrentXP = 0;

    UPROPERTY()
    int32 XP_CurrentLevel = 0;
};
```

### `Polarity/Save/SaveGameSubsystem.h`

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Map.h"
#include "Math/Range.h"
#include "SaveGameSubsystem.generated.h"

class UPolarityMetaSave;
class UPolarityRunSave;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnDesktopStateChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSaveLoaded);

/**
 * Sole owner of disk I/O. Loads Polarity_Meta synchronously on boot; orchestrates gather/scatter
 * to/from the live GameInstance subsystems; versioning, atomic write + backup, debounced async saves.
 */
UCLASS()
class POLARITY_API USaveGameSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // ============ Meta lifecycle ============
    UFUNCTION(BlueprintCallable, Category = "Save|Meta")
    void SaveMetaNow();      // sync flush; quit / high-value transitions

    UFUNCTION(BlueprintCallable, Category = "Save|Meta")
    void RequestMetaSave();  // debounced async; hot path (donations, chat read)

    UFUNCTION(BlueprintCallable, Category = "Save")
    void WipeMeta();         // "delete my progress" (clears meta slot + .bak)

    UPolarityMetaSave* GetMeta() const { return Meta; }

    // ============ Tutorial (req #1) вЂ” called by the TUTORIAL LEVEL BP/trigger, NOT UTutorialSubsystem ============
    /** Idempotent. First call: sets bTutorialCompleted, unlocks "raid" app + "post_tutorial" chat group,
     *  flushes meta synchronously, broadcasts OnDesktopStateChanged. Re-entry / replay: no-op. */
    UFUNCTION(BlueprintCallable, Category = "Save|Tutorial")
    void NotifyTutorialCompleted();

    UFUNCTION(BlueprintPure, Category = "Save|Tutorial")
    bool IsTutorialCompleted() const;

    // ============ Desktop apps (req #2) ============
    UFUNCTION(BlueprintPure, Category = "Save|Desktop")
    bool IsAppUnlocked(FName AppId) const;

    UFUNCTION(BlueprintCallable, Category = "Save|Desktop")
    void UnlockApp(FName AppId);

    UFUNCTION(BlueprintPure, Category = "Save|Desktop")
    TSet<FName> GetUnlockedApps() const;

    // ============ Chat read-state (req #3) ============
    UFUNCTION(BlueprintPure, Category = "Save|Chat")
    TSet<FName> GetUnlockedChatGroups() const;

    /** True the FIRST time a group is viewed (unlocked AND not yet seen) -> play with delays. */
    UFUNCTION(BlueprintPure, Category = "Save|Chat")
    bool ShouldPlayChatRealtime(FName GroupId) const;

    UFUNCTION(BlueprintPure, Category = "Save|Chat")
    int32 GetUnreadChatCount() const;

    UFUNCTION(BlueprintPure, Category = "Save|Chat")
    bool HasUnreadChat() const { return GetUnreadChatCount() > 0; }

    UFUNCTION(BlueprintCallable, Category = "Save|Chat")
    void UnlockChatGroup(FName GroupId);

    UFUNCTION(BlueprintCallable, Category = "Save|Chat")
    void MarkChatGroupSeen(FName GroupId); // after realtime playback finishes

    UFUNCTION(BlueprintCallable, Category = "Save|Chat")
    void MarkChatRead(int32 UpToIndex);    // high-water mark, clamped, never backward

    // ============ Run resume (req #4 mid-run) ============
    UFUNCTION(BlueprintPure, Category = "Save|Run")
    bool HasRunSave() const;

    UFUNCTION(BlueprintPure, Category = "Save|Run")
    int32 GetResumeArenaIndex() const;     // peek without full scatter

    UFUNCTION(BlueprintCallable, Category = "Save|Run")
    bool ResumeRun();                      // load run slot, scatter to Run+XP subsystems

    UFUNCTION(BlueprintCallable, Category = "Save|Run")
    void SaveRun();                        // checkpoint write (StartRun / per-arena / quit)

    UFUNCTION(BlueprintCallable, Category = "Save|Run")
    void ClearRun();                       // DeleteGameInSlot(run)

    // ============ UI change notifications (no per-frame polling) ============
    UPROPERTY(BlueprintAssignable, Category = "Save")
    FOnDesktopStateChanged OnDesktopStateChanged;

    UPROPERTY(BlueprintAssignable, Category = "Save")
    FOnSaveLoaded OnSaveLoaded;

private:
    // ---- In-memory authority ----
    UPROPERTY()
    TObjectPtr<UPolarityMetaSave> Meta = nullptr;

    bool bMetaLoaded = false;   // GUARD: never auto-save before initial load completes
    bool bMetaDirty  = false;
    bool bSaveInFlight = false;  // single in-flight save/load guard
    FTimerHandle MetaFlushTimer;

    // Group membership is CONTENT, not save state. Built at init from config, keyed by the
    // CSV '---' index: intro = [0,8], post_tutorial = [9,26].
    TMap<FName, FInt32Range> ChatGroupRanges;

    // ---- Slot names (PIE-isolated) ----
    static FString MetaSlotName();
    static FString RunSlotName();

    // ---- Gather / scatter ----
    void GatherMeta(UPolarityMetaSave& Out) const;
    void ApplyMeta(const UPolarityMetaSave& In);
    void GatherRun(UPolarityRunSave& Out) const;
    void ApplyRun(const UPolarityRunSave& In);

    // ---- Versioning / integrity ----
    void MigrateMeta(UPolarityMetaSave& S) const;
    void MigrateRun(UPolarityRunSave& S) const;
    static uint32 ComputeChecksum(const USaveGame& Obj);

    // ---- Disk plumbing ----
    void LoadMetaSynchronous();
    void FlushMetaIfDirty();
    void WriteSlotAtomic(USaveGame* Obj, const FString& Slot, bool bSync);
    void BindAppLifecycleDelegates();

    int32 GetMaxUnlockedChatRowIndex() const;

    template <typename T>
    T* GetGI() const
    {
        if (const UGameInstance* GI = GetGameInstance())
        {
            return GI->GetSubsystem<T>();
        }
        return nullptr;
    }
};
```

### `Polarity/Save/SaveGameSubsystem.cpp` (core logic; subsystem-specific gather/scatter abbreviated where it calls existing getters)

```cpp
#include "SaveGameSubsystem.h"

#include "PolarityMetaSave.h"
#include "PolarityRunSave.h"

#include "Kismet/GameplayStatics.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/Crc.h"
#include "Misc/DateTime.h"
#include "TimerManager.h"
#include "Engine/World.h"

// Project subsystems we read/write:
#include "Variant_Shooter/Run/RunSubsystem.h"
#include "Variant_Shooter/Stream/StreamSubsystem.h"
#include "Variant_Shooter/XP/XPSubsystem.h"
#include "Variant_Shooter/Lore/LoreSubsystem.h"

namespace
{
    constexpr int32 kUserIndex = 0;
    const FString kRaidAppId         = TEXT("raid");
    const FName   kPostTutorialGroup = TEXT("post_tutorial");
    const FName   kIntroGroup        = TEXT("intro");

    // Default streamer handle moved here (see Open Decisions). Empty saved string => use this.
    const TCHAR* kDefaultStreamerName = TEXT("@ramless_");
}

FString USaveGameSubsystem::MetaSlotName()
{
#if WITH_EDITOR
    return TEXT("Polarity_Meta_PIE");
#else
    return TEXT("Polarity_Meta");
#endif
}

FString USaveGameSubsystem::RunSlotName()
{
#if WITH_EDITOR
    return TEXT("Polarity_Run_PIE");
#else
    return TEXT("Polarity_Run");
#endif
}

void USaveGameSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    // Group membership: content config keyed by the CSV '---' row index. Hardcoded for now;
    // promote to a UDataAsset if designers need to edit it.
    ChatGroupRanges.Add(kIntroGroup,        FInt32Range(0, FInt32RangeBound::Inclusive(8)));
    ChatGroupRanges.Add(kPostTutorialGroup, FInt32Range(9, FInt32RangeBound::Inclusive(26)));

    LoadMetaSynchronous(); // sets bMetaLoaded; desktop first frame reads correct state
    BindAppLifecycleDelegates();

    OnSaveLoaded.Broadcast();
}

void USaveGameSubsystem::Deinitialize()
{
    FlushMetaIfDirty(); // backstop only; ApplicationWillDeactivate is the reliable path
    Super::Deinitialize();
}

// ---------------------------------------------------------------------------
// Load
// ---------------------------------------------------------------------------
void USaveGameSubsystem::LoadMetaSynchronous()
{
    const FString Slot = MetaSlotName();

    if (UGameplayStatics::DoesSaveGameExist(Slot, kUserIndex))
    {
        if (UPolarityMetaSave* Loaded =
                Cast<UPolarityMetaSave>(UGameplayStatics::LoadGameFromSlot(Slot, kUserIndex)))
        {
            // Integrity: CRC over the bytes the engine just deserialized into typed fields.
            // (We re-serialize the loaded object to recompute deterministically.)
            const uint32 Now = ComputeChecksum(*Loaded);
            const bool bChecksumOk = (Loaded->PayloadChecksum == 0) || (Now == Loaded->PayloadChecksum);

            if (bChecksumOk)
            {
                MigrateMeta(*Loaded);
                Meta = Loaded;
            }
            else
            {
                // Try .bak fallback (rotation handled in WriteSlotAtomic). For brevity, fall to fresh.
                UE_LOG(LogTemp, Warning, TEXT("[SAVE_DEBUG] Meta checksum mismatch -> starting fresh."));
            }
        }
    }

    if (!Meta)
    {
        Meta = Cast<UPolarityMetaSave>(
            UGameplayStatics::CreateSaveGameObject(UPolarityMetaSave::StaticClass()));
    }

    // Range sanity + default-resolution.
    Meta->MetaCurrency = FMath::Max<int64>(0, Meta->MetaCurrency);
    if (Meta->PlayerStreamerName.IsEmpty())
    {
        Meta->PlayerStreamerName = kDefaultStreamerName;
    }

    bMetaLoaded = true;

    // Push loaded meta out to the live subsystems.
    ApplyMeta(*Meta);
}

// ---------------------------------------------------------------------------
// Gather / scatter (Meta) вЂ” via existing public subsystem API
// ---------------------------------------------------------------------------
void USaveGameSubsystem::GatherMeta(UPolarityMetaSave& Out) const
{
    if (UStreamSubsystem* Stream = GetGI<UStreamSubsystem>())
    {
        Out.MetaCurrency       = Stream->GetMetaCurrency();
        Out.CompletedRuns      = Stream->GetCompletedRuns();
        Out.PlayerStreamerName = Stream->GetPlayerStreamerName();
    }
    if (ULoreSubsystem* Lore = GetGI<ULoreSubsystem>())
    {
        Out.ConsumedLoreIDs = Lore->GetConsumedLoreIDs();
    }
    // bTutorialCompleted / UnlockedApps / Chat / AbilityBank are OWNED by the save object itself
    // (mutated through this subsystem's API), so the live `Meta` already holds them вЂ” copy across.
    Out.bTutorialCompleted = Meta->bTutorialCompleted;
    Out.UnlockedApps       = Meta->UnlockedApps;
    Out.Chat               = Meta->Chat;
    Out.AbilityBank        = Meta->AbilityBank;
    Out.UnlockedUpgrades   = Meta->UnlockedUpgrades;
    Out.UnlockedCosmetics  = Meta->UnlockedCosmetics;
    Out.StartingSkillBoosts = Meta->StartingSkillBoosts;
}

void USaveGameSubsystem::ApplyMeta(const UPolarityMetaSave& In)
{
    if (UStreamSubsystem* Stream = GetGI<UStreamSubsystem>())
    {
        Stream->SetMetaCurrency(In.MetaCurrency);          // add setters if absent (see Integration)
        Stream->SetCompletedRuns(In.CompletedRuns);
        Stream->SetPlayerStreamerName(In.PlayerStreamerName);
    }
    if (ULoreSubsystem* Lore = GetGI<ULoreSubsystem>())
    {
        Lore->RestoreConsumedLoreIDs(In.ConsumedLoreIDs);
    }
    // Save-owned fields stay on `Meta`; no subsystem to push them to.
}

// ---------------------------------------------------------------------------
// Save (Meta)
// ---------------------------------------------------------------------------
void USaveGameSubsystem::SaveMetaNow()
{
    if (!bMetaLoaded || !Meta) { return; }
    GatherMeta(*Meta);
    Meta->SavedAtUtc = FDateTime::UtcNow();
    Meta->PayloadChecksum = 0;
    Meta->PayloadChecksum = ComputeChecksum(*Meta);
    WriteSlotAtomic(Meta, MetaSlotName(), /*bSync*/ true);
    bMetaDirty = false;
}

void USaveGameSubsystem::RequestMetaSave()
{
    if (!bMetaLoaded) { return; }
    bMetaDirty = true;

    if (UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr)
    {
        if (!World->GetTimerManager().IsTimerActive(MetaFlushTimer))
        {
            World->GetTimerManager().SetTimer(
                MetaFlushTimer, this, &USaveGameSubsystem::FlushMetaIfDirty, 5.0f, /*loop*/ false);
        }
    }
}

void USaveGameSubsystem::FlushMetaIfDirty()
{
    if (bMetaDirty && bMetaLoaded && Meta)
    {
        GatherMeta(*Meta);
        Meta->SavedAtUtc = FDateTime::UtcNow();
        Meta->PayloadChecksum = 0;
        Meta->PayloadChecksum = ComputeChecksum(*Meta);
        WriteSlotAtomic(Meta, MetaSlotName(), /*bSync*/ false); // async on the hot path
        bMetaDirty = false;
    }
}

// ---------------------------------------------------------------------------
// Tutorial (req #1)
// ---------------------------------------------------------------------------
void USaveGameSubsystem::NotifyTutorialCompleted()
{
    if (!Meta || Meta->bTutorialCompleted)
    {
        return; // idempotent: at most one disk write on the first transition
    }

    Meta->bTutorialCompleted = true;
    Meta->UnlockedApps.Add(FName(*kRaidAppId));
    Meta->Chat.UnlockedGroups.Add(kPostTutorialGroup);

    SaveMetaNow(); // high-value transition -> immediate sync flush
    OnDesktopStateChanged.Broadcast();
}

bool USaveGameSubsystem::IsTutorialCompleted() const
{
    return Meta && Meta->bTutorialCompleted;
}

// ---------------------------------------------------------------------------
// Desktop apps (req #2)
// ---------------------------------------------------------------------------
bool USaveGameSubsystem::IsAppUnlocked(FName AppId) const
{
    return Meta && Meta->UnlockedApps.Contains(AppId);
}

void USaveGameSubsystem::UnlockApp(FName AppId)
{
    if (Meta && !Meta->UnlockedApps.Contains(AppId))
    {
        Meta->UnlockedApps.Add(AppId);
        RequestMetaSave();
        OnDesktopStateChanged.Broadcast();
    }
}

TSet<FName> USaveGameSubsystem::GetUnlockedApps() const
{
    return Meta ? Meta->UnlockedApps : TSet<FName>();
}

// ---------------------------------------------------------------------------
// Chat read-state (req #3)
// ---------------------------------------------------------------------------
TSet<FName> USaveGameSubsystem::GetUnlockedChatGroups() const
{
    return Meta ? Meta->Chat.UnlockedGroups : TSet<FName>();
}

bool USaveGameSubsystem::ShouldPlayChatRealtime(FName GroupId) const
{
    return Meta
        && Meta->Chat.UnlockedGroups.Contains(GroupId)
        && !Meta->Chat.SeenGroups.Contains(GroupId);
}

int32 USaveGameSubsystem::GetMaxUnlockedChatRowIndex() const
{
    int32 Max = -1;
    if (Meta)
    {
        for (const FName& G : Meta->Chat.UnlockedGroups)
        {
            if (const FInt32Range* R = ChatGroupRanges.Find(G))
            {
                Max = FMath::Max(Max, R->GetUpperBoundValue());
            }
        }
    }
    return Max;
}

int32 USaveGameSubsystem::GetUnreadChatCount() const
{
    const int32 MaxIdx = GetMaxUnlockedChatRowIndex();
    if (MaxIdx < 0 || !Meta) { return 0; }
    return FMath::Max(0, MaxIdx - Meta->Chat.LastReadMessageIndex);
}

void USaveGameSubsystem::UnlockChatGroup(FName GroupId)
{
    if (Meta && !Meta->Chat.UnlockedGroups.Contains(GroupId))
    {
        Meta->Chat.UnlockedGroups.Add(GroupId);
        RequestMetaSave();
        OnDesktopStateChanged.Broadcast();
    }
}

void USaveGameSubsystem::MarkChatGroupSeen(FName GroupId)
{
    if (Meta && !Meta->Chat.SeenGroups.Contains(GroupId))
    {
        Meta->Chat.SeenGroups.Add(GroupId);
        RequestMetaSave();
    }
}

void USaveGameSubsystem::MarkChatRead(int32 UpToIndex)
{
    if (Meta && UpToIndex > Meta->Chat.LastReadMessageIndex)
    {
        Meta->Chat.LastReadMessageIndex = FMath::Min(UpToIndex, GetMaxUnlockedChatRowIndex());
        RequestMetaSave();
        OnDesktopStateChanged.Broadcast();
    }
}

// ---------------------------------------------------------------------------
// Run slot
// ---------------------------------------------------------------------------
bool USaveGameSubsystem::HasRunSave() const
{
    return UGameplayStatics::DoesSaveGameExist(RunSlotName(), kUserIndex);
}

void USaveGameSubsystem::SaveRun()
{
    UPolarityRunSave* RunSave = Cast<UPolarityRunSave>(
        UGameplayStatics::CreateSaveGameObject(UPolarityRunSave::StaticClass()));
    GatherRun(*RunSave);
    RunSave->PayloadChecksum = 0;
    RunSave->PayloadChecksum = ComputeChecksum(*RunSave);
    WriteSlotAtomic(RunSave, RunSlotName(), /*bSync*/ false);
}

bool USaveGameSubsystem::ResumeRun()
{
    if (!HasRunSave()) { return false; }

    UPolarityRunSave* Loaded =
        Cast<UPolarityRunSave>(UGameplayStatics::LoadGameFromSlot(RunSlotName(), kUserIndex));
    if (!Loaded) { return false; }

    const uint32 Now = ComputeChecksum(*Loaded);
    if (Loaded->PayloadChecksum != 0 && Now != Loaded->PayloadChecksum)
    {
        // Run slot is expendable: discard corrupt resume, offer fresh run.
        ClearRun();
        return false;
    }

    MigrateRun(*Loaded);
    ApplyRun(*Loaded);
    return true;
}

void USaveGameSubsystem::ClearRun()
{
    if (HasRunSave())
    {
        UGameplayStatics::DeleteGameInSlot(RunSlotName(), kUserIndex);
    }
}

int32 USaveGameSubsystem::GetResumeArenaIndex() const
{
    if (HasRunSave())
    {
        if (const UPolarityRunSave* Loaded =
                Cast<UPolarityRunSave>(UGameplayStatics::LoadGameFromSlot(RunSlotName(), kUserIndex)))
        {
            return Loaded->CurrentArenaIndex;
        }
    }
    return -1;
}

void USaveGameSubsystem::GatherRun(UPolarityRunSave& Out) const
{
    if (URunSubsystem* Run = GetGI<URunSubsystem>())
    {
        Out.RunState              = static_cast<uint8>(Run->GetRunState());
        Out.CurrentArenaIndex     = Run->GetCurrentArenaIndex();
        Out.ActivatedAntennaCount = Run->GetActivatedAntennaCount();
        Out.AcquiredUpgrades      = Run->GetAcquiredUpgrades();

        const FRunStats& Stats = Run->GetStats();
        Out.TotalXPEarned = Stats.TotalXPEarned;
        Out.LevelsGained  = Stats.LevelsGained;
        Out.RunDuration   = Stats.RunDuration;

        Out.KillsByEnemyClassPath.Reset();
        for (const TPair<TSubclassOf<AShooterNPC>, int32>& Kill : Stats.KillsByEnemy)
        {
            if (*Kill.Key)
            {
                Out.KillsByEnemyClassPath.Add(FSoftObjectPath(*Kill.Key).ToString(), Kill.Value);
            }
        }
    }
    if (UXPSubsystem* XP = GetGI<UXPSubsystem>())
    {
        const FSkillState& P = XP->GetProgress();
        Out.XP_CurrentXP    = P.CurrentXP;
        Out.XP_CurrentLevel = P.CurrentLevel;
    }
}

void USaveGameSubsystem::ApplyRun(const UPolarityRunSave& In)
{
    if (URunSubsystem* Run = GetGI<URunSubsystem>())
    {
        FRunStats Stats;
        Stats.TotalXPEarned = In.TotalXPEarned;
        Stats.LevelsGained  = In.LevelsGained;
        Stats.RunDuration   = In.RunDuration;
        for (const TPair<FString, int32>& Kv : In.KillsByEnemyClassPath)
        {
            TSoftClassPtr<AShooterNPC> SoftClass(FSoftObjectPath(Kv.Key));
            if (UClass* Resolved = SoftClass.LoadSynchronous())
            {
                Stats.KillsByEnemy.Add(Resolved, Kv.Value);
            }
            // unresolved (renamed/deleted BP) -> dropped, acceptable for run-scoped stats
        }

        Run->RestoreFromSave(
            static_cast<ERunState>(In.RunState),
            In.CurrentArenaIndex,
            In.ActivatedAntennaCount,
            In.AcquiredUpgrades,
            Stats); // single restore entry point added on URunSubsystem (see Integration)
    }
    if (UXPSubsystem* XP = GetGI<UXPSubsystem>())
    {
        XP->RestoreProgress(In.XP_CurrentXP, In.XP_CurrentLevel);
    }
}

// ---------------------------------------------------------------------------
// Versioning
// ---------------------------------------------------------------------------
void USaveGameSubsystem::MigrateMeta(UPolarityMetaSave& S) const
{
    while (S.SaveVersion < PolaritySaveVersion::MetaLatest)
    {
        switch (S.SaveVersion)
        {
        // case 1: Migrate_1_to_2(S); S.SaveVersion = 2; break;
        default:
            UE_LOG(LogTemp, Warning,
                TEXT("[SAVE_DEBUG] Meta save v%d newer than supported v%d -> quarantine, fresh."),
                S.SaveVersion, PolaritySaveVersion::MetaLatest);
            return; // never downgrade
        }
    }
}

void USaveGameSubsystem::MigrateRun(UPolarityRunSave& S) const
{
    while (S.SaveVersion < PolaritySaveVersion::RunLatest)
    {
        switch (S.SaveVersion)
        {
        default:
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// Integrity + atomic write
// ---------------------------------------------------------------------------
uint32 USaveGameSubsystem::ComputeChecksum(const USaveGame& Obj)
{
    // Deterministic: serialize SaveGame fields to bytes, CRC the buffer. We exclude the
    // PayloadChecksum field by zeroing it before calling (callers do this).
    TArray<uint8> Bytes;
    FMemoryWriter Writer(Bytes, /*bIsPersistent*/ true);
    FObjectAndNameAsStringProxyArchive Ar(Writer, /*bLoadIfFindFails*/ false);
    Ar.ArIsSaveGame = true;
    Ar.ArNoDelta = true;
    const_cast<USaveGame&>(Obj).Serialize(Ar);
    return FCrc::MemCrc32(Bytes.GetData(), Bytes.Num());
}

void USaveGameSubsystem::WriteSlotAtomic(USaveGame* Obj, const FString& Slot, bool bSync)
{
    if (!Obj || bSaveInFlight) { return; }

    const FString Dir  = FPaths::ProjectSavedDir() / TEXT("SaveGames/");
    const FString Tmp  = Slot + TEXT("_tmp");
    const FString Live = Dir + Slot + TEXT(".sav");
    const FString Bak  = Dir + Slot + TEXT(".bak");
    const FString TmpF = Dir + Tmp  + TEXT(".sav");

    auto Promote = [Dir, Live, Bak, TmpF]()
    {
        IFileManager& FM = IFileManager::Get();
        if (FM.FileExists(*Live))
        {
            FM.Move(*Bak, *Live, /*Replace*/ true, /*EvenIfReadOnly*/ true);
        }
        FM.Move(*Live, *TmpF, /*Replace*/ true, /*EvenIfReadOnly*/ true);
    };

    if (bSync)
    {
        if (UGameplayStatics::SaveGameToSlot(Obj, Tmp, kUserIndex))
        {
            Promote();
        }
    }
    else
    {
        bSaveInFlight = true;
        FAsyncSaveGameToSlotDelegate Done;
        Done.BindLambda(
            [this, Promote](const FString&, const int32, bool bSuccess)
            {
                if (bSuccess) { Promote(); }
                bSaveInFlight = false;
            });
        UGameplayStatics::AsyncSaveGameToSlot(Obj, Tmp, kUserIndex, Done);
    }
}

void USaveGameSubsystem::WipeMeta()
{
    UGameplayStatics::DeleteGameInSlot(MetaSlotName(), kUserIndex);
    Meta = Cast<UPolarityMetaSave>(
        UGameplayStatics::CreateSaveGameObject(UPolarityMetaSave::StaticClass()));
    Meta->PlayerStreamerName = kDefaultStreamerName;
    bMetaDirty = false;
    ApplyMeta(*Meta);
    OnDesktopStateChanged.Broadcast();
}

void USaveGameSubsystem::BindAppLifecycleDelegates()
{
    FCoreDelegates::ApplicationWillDeactivateDelegate.AddWeakLambda(
        this, [this]() { SaveMetaNow(); });
    FCoreDelegates::ApplicationWillTerminateDelegate.AddWeakLambda(
        this, [this]() { SaveMetaNow(); });
}
```

---

## Integration points

**`URunSubsystem`** (header touch = rebuild): add a single `RestoreFromSave(ERunState, int32 ArenaIndex, int32 Antenna, const TMap<FGameplayTag,int32>& Upgrades, const FRunStats& Stats)` and confirm public getters (`GetRunState/GetCurrentArenaIndex/GetActivatedAntennaCount/GetAcquiredUpgrades/GetStats`). In `.cpp`: after `StartRun` reset в†’ `SaveGameSubsystem->SaveRun()`; `EnterArena/ClearArena` в†’ `SaveRun()`; `EndRun(reason)` в†’ bank rewards into `UStreamSubsystem` в†’ `SaveMetaNow()` в†’ `ClearRun()` on death/victory, or final `SaveRun()` + keep on quit. Call `InitializeDependency(USaveGameSubsystem::StaticClass())`.

**`UStreamSubsystem`** (cpp-only if setters exist): needs `SetMetaCurrency/SetCompletedRuns/SetPlayerStreamerName` for scatter (add if absent в†’ header touch). Hook `OnMetaCurrencyChanged` and `MarkRunMilestoneReached` в†’ `RequestMetaSave()`. `InitializeDependency`.

**`UXPSubsystem`** (cpp-only if `RestoreProgress` exists): `GetProgress()` for gather, `RestoreProgress(int32 XP, int32 Level)` for scatter (add if absent в†’ header touch). `InitializeDependency`.

**`ULoreSubsystem`** (cpp-only if API exists): `GetConsumedLoreIDs()` + `RestoreConsumedLoreIDs(const TSet<FName>&)`. Hook `MarkLoreConsumed` в†’ `RequestMetaSave()`. `InitializeDependency`.

**Tutorial LEVEL (content, no source change):** the level BP / a guaranteed end-of-level `ATriggerBox` calls `Get Game Instance Subsystem (USaveGameSubsystem) в†’ NotifyTutorialCompleted` on the mandatory exit, BEFORE `OpenLevel("MainMenu")`. Do NOT route through `UTutorialSubsystem::OnTutorialCompleted` (per-hint, per-session). `UTutorialSubsystem` is untouched and stays session-scoped.

**Desktop UMG (content):** on construct, bind `OnDesktopStateChanged`; Raid icon visibility = `IsAppUnlocked("raid")`; "Resume Run" affordance = `HasRunSave()` в†’ on click `ResumeRun()` then `OpenLevel` for `GetResumeArenaIndex()`.

**Chat UMG (content):** on open, for each group in `GetUnlockedChatGroups()` in order: if `ShouldPlayChatRealtime(group)` в†’ play rows honoring `FChatMessage::DelayBefore`, then `MarkChatGroupSeen(group)`; else dump instantly. On scroll-to-bottom/close в†’ `MarkChatRead(GetMaxUnlockedChatRowIndex())`. Unread badge binds to `GetUnreadChatCount()`.

---

## Implementation steps

1. **(rebuild)** Add `Polarity/Save/` foundation: `PolaritySaveTypes.h`, `PolarityMetaSave.h/.cpp`, `PolarityRunSave.h/.cpp`, `SaveGameSubsystem.h/.cpp`. One full rebuild. Verify it compiles and `Initialize` loads/creates the meta slot (PIE).
2. **(rebuild)** Add scatter/restore accessors where missing: `URunSubsystem::RestoreFromSave`, `UXPSubsystem::RestoreProgress`, `ULoreSubsystem::RestoreConsumedLoreIDs`, `UStreamSubsystem::Set*`. Add `InitializeDependency(USaveGameSubsystem)` to each (`.cpp`). Same rebuild as step 1 if batched.
3. **(cpp-only)** Wire run lifecycle hooks in `URunSubsystem.cpp` (`StartRun/EnterArena/ClearArena/EndRun`).
4. **(cpp-only)** Wire meta dirty hooks (`OnMetaCurrencyChanged`, `MarkRunMilestoneReached`, `MarkLoreConsumed`).
5. **(content)** Tutorial-level end trigger в†’ `NotifyTutorialCompleted`. Verify `bTutorialCompleted` + raid app + `post_tutorial` group persist across PIE restart.
6. **(content)** Desktop + chat UMG bind to the API; verify unread math (fresh `intro` в†’ 9; post-tutorial в†’ 18; restart в†’ instant dump + persisted raid icon).
7. **(cpp-only, recommended)** Add the gatherв†’scatter round-trip unit test (populate meta+run, write, reload, assert equality) as a CI gate.

---

## Open decisions for the user

1. **Single slot (two sections) vs two slots.** *Recommendation: two slots* (`Polarity_Meta`, `Polarity_Run`). It physically isolates corruption (a bad high-frequency run write can't brick meta) and makes "abandon run" one `DeleteGameInSlot`. Chosen above.
2. **Async vs sync writes.** *Recommendation: hybrid* вЂ” `AsyncSaveGameToSlot` (debounced ~5s) for hot-path meta + per-arena run checkpoints; synchronous `SaveGameToSlot` only on quit / `ApplicationWillDeactivate` / tutorial-complete. Chosen above.
3. **Where the `@ramless_` default lives** now that it's removed from the field. *Recommendation:* keep an empty default on the save field; resolve to the code constant `kDefaultStreamerName` (`@ramless_`) in `LoadMetaSynchronous` when the saved string is empty (implemented above). This avoids the "blank save string overwrites the intended default" bug (INTEL #1). If you intend to drop the handle entirely, set `kDefaultStreamerName = TEXT("")` and have the menu prompt for a name on first run.
4. **Chat group ranges: hardcoded vs `UDataAsset`.** *Recommendation:* hardcode `intro=[0,8]`, `post_tutorial=[9,26]` now (keyed off the CSV `---` column); promote to a `UDataAsset` only when designers need to add groups without a recompile. Treat the `---` ids as append-only/immutable; mid-list insertion is a `SaveVersion` bump.
5. **Mid-run resume granularity.** *Recommendation:* room/arena-boundary resume (return to start of `CurrentArenaIndex`), not mid-combat. True mid-fight save (enemy positions, in-flight projectiles) is out of scope and not needed for a Hades-like.
6. **Migration test gate.** *Recommendation:* yes вЂ” adopt the round-trip unit test (step 7) as mandatory before any save-schema change; it is the only net that catches the "forgot a gather/scatter copy line в†’ silent non-persistence" failure mode this design inherits.

---

## Rebuild impact summary

- **One full ~10-min rebuild** up front: the 4 new headers under `Polarity/Save/` plus the one-time accessor/restore declarations on `RunSubsystem.h`, `XPSubsystem.h`, `LoreSubsystem.h`, `StreamSubsystem.h` (batch these into a single compile).
- **After that, Live-Coding-friendly:** all lifecycle wiring, gather/scatter bodies, chat math, migration steps, and atomic-write logic live in `.cpp`.
- **Recurring header cost:** populating the reserved req #4 meta fields (`AbilityBank`, `UnlockedUpgrades`, etc.) is `.cpp`-only because the containers already exist on `UPolarityMetaSave`. A *genuinely new* persisted scalar means editing `UPolarityMetaSave.h` (a small, isolated `USaveGame` header вЂ” cheap, no gameplay dependents) в†’ one rebuild. Batch such additions.
- **Existing `UPROPERTY(SaveGame)` fields:** reused as-is (read/written through public getters/setters); none re-typed or renamed, so no churn on the expensive gameplay subsystem headers beyond the one-time accessor additions.

рџ“ќ Design deliverable only вЂ” no project files modified. Implementation would add `Polarity/Save/PolaritySaveTypes.h`, `PolarityMetaSave.h/.cpp`, `PolarityRunSave.h/.cpp`, `SaveGameSubsystem.h/.cpp` (new headers в†’ one full rebuild), plus one-time accessor declarations on `RunSubsystem.h`, `XPSubsystem.h`, `LoreSubsystem.h`, `StreamSubsystem.h` (header touch в†’ same rebuild); thereafter Live-Coding compatible.
