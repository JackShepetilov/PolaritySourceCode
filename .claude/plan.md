# Plan: Destructible Flying Island Arena System

## Overview

Система разрушаемых летающих островов-арен, которые можно уничтожить дистанционно через:
- EMF Projectile (на высокой скорости)
- EMF Physics Prop (на высокой скорости, особенно в reverse flight)
- Player Melee (удар ближнего боя при подлёте на высокой скорости)

Разрушение триггерит конец арены (ForceCompleteArena) и может выдать награду через BP делегат.

---

## Step 1: Create `ADestructibleIslandActor` (new files)

**New files:** `Polarity/Arena/DestructibleIslandActor.h`, `DestructibleIslandActor.cpp`

### Header structure:
```cpp
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnIslandDestroyed, ADestructibleIslandActor*, Island, AActor*, Destroyer);

UCLASS()
class ADestructibleIslandActor : public AActor
{
    // --- Components ---
    UStaticMeshComponent* IslandMesh;     // Main platform mesh (visible, walkable)

    // --- Config ---
    float IslandHP = 500.f;               // Current HP
    float MaxIslandHP = 500.f;            // Max HP
    float MinImpactSpeed = 1500.f;        // Min speed for damage (projectile/prop)
    float MinMeleeSpeed = 800.f;          // Min player speed for melee to count
    float DamagePerSpeed = 1.0f;          // Damage = (Speed - MinSpeed) * DamagePerSpeed
    FName IslandID;                       // Unique ID for persistence
    TSubclassOf<AActor> DestroyedEffectClass; // BP actor to spawn on destruction (VFX/debris)

    // --- State ---
    bool bIsDestroyed = false;

    // --- Delegates ---
    FOnIslandDestroyed OnIslandDestroyed;  // BlueprintAssignable, fires on destruction

    // --- Methods ---
    void OnIslandHit(UPrimitiveComponent*, AActor*, UPrimitiveComponent*, FVector, const FHitResult&);
    void TakeImpactDamage(float Speed, AActor* DamageCauser);
    void DestroyIsland(AActor* Destroyer);
    virtual float TakeDamage(...) override;  // For melee damage path
};
```

### Collision logic:

**OnComponentHit (for projectiles & props):**
- Check OtherActor is `AEMFProjectile` or `AEMFPhysicsProp`
- Get speed: Projectile → `GetVelocity().Size()`, Prop → `PropMesh->GetPhysicsLinearVelocity().Size()`
- If speed >= MinImpactSpeed → `TakeImpactDamage(Speed, OtherActor)`

**TakeDamage override (for melee):**
- Check DamageType is melee (`UDamageType_Melee`, `UDamageType_MomentumBonus`, `UDamageType_Dropkick`)
- Check DamageCauser (player) velocity >= MinMeleeSpeed
- Accept damage, reduce HP
- HP <= 0 → `DestroyIsland(DamageCauser)`

**TakeImpactDamage(Speed, Attacker):**
- Calculate damage: `(Speed - MinImpactSpeed) * DamagePerSpeed`
- Reduce HP
- HP <= 0 → `DestroyIsland(Attacker)`

**DestroyIsland(Destroyer):**
- Set `bIsDestroyed = true`
- Hide mesh, disable collision
- Spawn DestroyedEffectClass (BP с VFX, обломками, Niagara)
- Broadcast `OnIslandDestroyed(this, Destroyer)`

### Actor Tag:
- В конструкторе: `Tags.Add(TEXT("MeleeDestructible"))` — для того чтобы MeleeAttackComponent его видел

### Collision Setup:
- IslandMesh: BlockAll profile (player walks on it, projectiles/props hit it)
- `SetNotifyRigidBodyCollision(true)` для OnComponentHit
- CCD не нужен (остров статичен, CCD нужен движущемуся объекту)

---

## Step 2: Modify `MeleeAttackComponent` — support hitting destructible objects

**Modified file:** `MeleeAttackComponent.cpp`

**Change in `IsValidMeleeTarget()` (line ~548):**

Before:
```cpp
// Check if it implements IShooterDummyTarget
if (HitActor->Implements<UShooterDummyTarget>())
{
    return true;
}
return false;
```

After:
```cpp
// Check if it implements IShooterDummyTarget
if (HitActor->Implements<UShooterDummyTarget>())
{
    return true;
}

// Check if it's a destructible environment target (islands, etc.)
if (HitActor->ActorHasTag(TEXT("MeleeDestructible")))
{
    return true;
}

return false;
```

**Это единственное изменение в MeleeAttackComponent.** Sweep trace уже находит StaticMesh с BlockAll (блокирует ECC_Pawn). Тег позволяет пройти фильтр IsValidMeleeTarget. Остальная логика (ApplyDamage → TakeDamage на острове) работает через существующий путь.

---

## Step 3: Modify `ArenaManager` — add `ForceCompleteArena()`

**Modified files:** `ArenaManager.h`, `ArenaManager.cpp`

### Header addition:
```cpp
// Optional reference to destructible island in this arena's sublevel
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Island")
TSoftObjectPtr<ADestructibleIslandActor> LinkedIsland;

UFUNCTION(BlueprintCallable, Category = "Arena")
void ForceCompleteArena();
```

### Implementation:
```cpp
void AArenaManager::ForceCompleteArena()
{
    if (CurrentState == EArenaState::Completed) return;

    // Kill all remaining NPCs
    for (auto& NPCPtr : AliveNPCs)
    {
        if (AShooterNPC* NPC = NPCPtr.Get())
        {
            if (NPC->IsAlive())
            {
                // Lethal damage — triggers normal death flow (ragdoll, OnNPCDeath, etc.)
                UGameplayStatics::ApplyDamage(NPC, 99999.f, nullptr, nullptr, UDamageType::StaticClass());
            }
        }
    }

    // Clear timers (might be between waves)
    GetWorldTimerManager().ClearTimer(WaveTimerHandle);

    CompleteArena();
}
```

### BeginPlay binding (optional, can also be done in BP):
```cpp
// In BeginPlay, after existing code:
if (ADestructibleIslandActor* Island = LinkedIsland.Get())
{
    Island->OnIslandDestroyed.AddDynamic(this, &AArenaManager::OnLinkedIslandDestroyed);
}

void AArenaManager::OnLinkedIslandDestroyed(ADestructibleIslandActor* Island, AActor* Destroyer)
{
    ForceCompleteArena();
}
```

---

## Step 4: Persistence (within session)

Для сохранения состояния между стримингом подуровней (sublevel unload/reload):

**Вариант A — GameInstance Subsystem (рекомендуемый):**
- `UDestroyedIslandsSubsystem : UGameInstanceSubsystem`
- `TSet<FName> DestroyedIslandIDs`
- DestructibleIslandActor в BeginPlay проверяет: если его IslandID в списке → сразу DestroyIsland() без VFX
- При разрушении → регистрирует ID в subsystem

**Вариант B — SaveGame (для кросс-сессионной персистенции):**
- `UIslandSaveGame : USaveGame` с `TSet<FName>`
- Subsystem загружает/сохраняет при старте/изменении
- Более сложный, но нужен если "навсегда" = между запусками игры

Рекомендую начать с варианта A (GameInstance subsystem), добавить SaveGame позже при необходимости.

---

## Files Summary

| Action | File | Changes |
|--------|------|---------|
| CREATE | `Arena/DestructibleIslandActor.h` | New class — destructible island actor |
| CREATE | `Arena/DestructibleIslandActor.cpp` | Implementation — collision, damage, destruction |
| MODIFY | `Variant_Shooter/MeleeAttackComponent.cpp` | +4 lines in IsValidMeleeTarget (tag check) |
| MODIFY | `Arena/ArenaManager.h` | +LinkedIsland property, +ForceCompleteArena(), +OnLinkedIslandDestroyed() |
| MODIFY | `Arena/ArenaManager.cpp` | +ForceCompleteArena() impl, +BeginPlay binding |
| CREATE | `Arena/DestroyedIslandsSubsystem.h` | Session persistence subsystem |
| CREATE | `Arena/DestroyedIslandsSubsystem.cpp` | TSet<FName> tracking |

---

## Performance Notes

- OnComponentHit fires only on actual physics collisions — zero cost when nothing is hitting the island
- TakeDamage is event-driven — zero tick cost
- No Tick override needed on DestructibleIslandActor
- Mesh swap + Niagara для VFX разрушения (настраивается в BP через DestroyedEffectClass)
- Subsystem — O(1) lookup в TSet при BeginPlay каждого острова

## What stays in Blueprint

- DestroyedEffectClass — BP actor с Niagara, звуками, обломками (Geometry Collection по желанию)
- Reward logic — через OnIslandDestroyed delegate или OnArenaCleared
- Visual tuning — mesh, VFX, particles всё в BP
- LinkedIsland reference — задаётся в Details panel ArenaManager в подуровне
