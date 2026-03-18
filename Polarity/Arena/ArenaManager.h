// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ArenaWaveData.h"
#include "ArenaManager.generated.h"

class UBoxComponent;
class AArenaSpawnPoint;
class AShooterNPC;
class AFlyingDrone;
class AShooterCharacter;
class UCheckpointSubsystem;
class AShooterDoor;
class ADestructibleIslandActor;
class AEMFPhysicsProp;
class AEMFProjectile;
class UGeometryCollection;
class UNiagaraSystem;
class AShooterDummy;
class ARewardContainer;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnArenaStarted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnArenaCleared);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWaveStarted, int32, WaveIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWaveCleared, int32, WaveIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnArenaCriticalImpact, AActor*, Source, FVector, Location, float, Speed);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnAllPropsDestroyed);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPropPercentChanged, float, RemainingPercent, int32, AliveCount);

/**
 * Manages a combat arena: activation, wave spawning, exit blockers, and checkpoint integration.
 *
 * Place one per arena level. Configure waves in the Details panel.
 * Blockers are separate mesh actors referenced by this manager (enabled only during combat).
 * Activation happens when the player overlaps a separate entry trigger.
 */
UCLASS(Blueprintable)
class POLARITY_API AArenaManager : public AActor
{
	GENERATED_BODY()

public:
	AArenaManager();

	// ==================== Arena Mode ====================

	/** How the arena spawns enemies */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Mode")
	EArenaMode ArenaMode = EArenaMode::Waves;

	// ==================== Wave Configuration ====================

	/** Waves of enemies to spawn, in order (used when ArenaMode == Waves) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Waves", meta = (EditCondition = "ArenaMode == EArenaMode::Waves"))
	TArray<FArenaWave> Waves;

	/** Pause between waves (seconds). Next wave auto-starts after this delay */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Waves", meta = (ClampMin = "0.0", EditCondition = "ArenaMode == EArenaMode::Waves"))
	float TimeBetweenWaves = 3.0f;

	// ==================== Sustain Configuration ====================

	/** Weighted pool of enemy classes for sustain mode */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Sustain", meta = (EditCondition = "ArenaMode == EArenaMode::Sustain"))
	TArray<FSustainSpawnEntry> SustainEnemyPool;

	/** Max enemies alive simultaneously in sustain mode.
	 *  0 = maintain the count of enemies that were manually placed on the level. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Sustain", meta = (ClampMin = "0", EditCondition = "ArenaMode == EArenaMode::Sustain"))
	int32 MaxSustainEnemies = 1;

	/** Total number of enemies to spawn before the arena completes.
	 *  -1 = infinite (never completes, current behavior).
	 *  Counts only respawned enemies, not the initial batch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Sustain", meta = (ClampMin = "-1", EditCondition = "ArenaMode == EArenaMode::Sustain"))
	int32 SustainTotalEnemies = -1;

	// ==================== Blockers ====================

	/**
	 * Actors that block arena exits during combat (mesh walls/doors).
	 * Fully disabled (invisible, no collision) until arena activates.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Blockers")
	TArray<TSoftObjectPtr<AActor>> ExitBlockers;

	// ==================== Entry Triggers ====================

	/**
	 * Trigger volumes that activate the arena when the player enters.
	 * Can be any actor with a primitive component (StaticMesh, Box/Sphere/CapsuleCollision, etc.).
	 * These are separate from blockers — blockers only appear after activation.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Activation")
	TArray<TSoftObjectPtr<AActor>> EntryTriggers;

	// ==================== Spawn Points ====================

	/** Spawn point markers placed around the arena */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Spawn")
	TArray<TSoftObjectPtr<AArenaSpawnPoint>> SpawnPoints;

	// ==================== Respawn ====================

	/** Where the player respawns if they die during this arena fight */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Respawn")
	TSoftObjectPtr<AActor> PlayerRespawnPoint;

	// ==================== Reward Door ====================

	/** Door that opens when all waves are cleared (e.g. upgrade room) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Door")
	TSoftObjectPtr<AShooterDoor> RewardDoor;

	// ==================== Reward Container ====================

	/** Optional dummy behind the reward door. When killed, the reward container activates. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Reward")
	TSoftObjectPtr<AShooterDummy> RewardDummy;

	/** Reward container that drops and shatters when the reward dummy dies. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Reward")
	TSoftObjectPtr<ARewardContainer> RewardContainer;

	// ==================== Tracked Props ====================

	/** EMF props to monitor for critical velocity impacts.
	 *  When any tracked prop hits at its CriticalVelocity, OnPropCriticalVelocityImpact fires. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Props")
	TArray<TSoftObjectPtr<AEMFPhysicsProp>> TrackedProps;

	/** If set, auto-finds ALL actors of this class on the level and tracks them as arena props.
	 *  Props found this way get death tracking + respawn on player death. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Props")
	TSubclassOf<AEMFPhysicsProp> AutoPropClass;

	// ==================== Destructible Island ====================

	/** Optional destructible island linked to this arena. Destroying it force-completes the arena. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Island")
	TSoftObjectPtr<ADestructibleIslandActor> LinkedIsland;

	// ==================== Arena Destruction ====================

	/** Actors EXCLUDED from arena destruction (floors, walls, pillars you want to keep).
	 *  Everything else with a StaticMeshComponent or EMFPhysicsProp within DestructionRadius gets shattered. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Destruction")
	TArray<TSoftObjectPtr<AActor>> DestructionExcluded;

	/** Radius (cm) from ArenaManager to auto-collect destructible meshes/props */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Destruction", meta = (ClampMin = "100.0", Units = "cm"))
	float DestructionRadius = 3000.0f;

	/** Generic cube GC asset — spawned at each mesh's size/rotation on destruction */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Destruction")
	TObjectPtr<UGeometryCollection> DestructionGC;

	/** Dust/debris VFX spawned at each destroyed mesh to mask the mesh→GC transition.
	 *  Create a Niagara System with a burst of smoke/dust sprites. Leave empty to skip. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Destruction")
	TObjectPtr<UNiagaraSystem> DestructionVFX;

	/** Half-extents of the DestructionGC asset in its native (unscaled) form.
	 *  Used to compute per-mesh scale. Set to match your cube GC asset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Destruction")
	FVector DestructionGCHalfExtent = FVector(50.0f);

	/** Shockwave speed (cm/s). Objects farther from epicenter break later. 0 = all instant. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Destruction", meta = (ClampMin = "0.0"))
	float DestructionWaveSpeed = 5000.0f;

	/** How long GC gib fragments persist before being destroyed (seconds). 0 = never destroy. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Destruction", meta = (ClampMin = "0.0"))
	float DestructionGibLifetime = 0.0f;

	/** Seconds after spawning to freeze gibs (disable physics). Saves CPU once debris settles.
	 *  Set to 0 to never freeze. Should be < GibLifetime. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Destruction", meta = (ClampMin = "0.0"))
	float DestructionGibFreezeTime = 2.0f;

	/** Impulse for GC gibs (cm/s). 0 = pure gravity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Destruction", meta = (ClampMin = "0.0"))
	float DestructionImpulse = 50.0f;

	/** Impulse for collapsing whole meshes (cm/s). Higher = more dramatic scatter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Destruction", meta = (ClampMin = "0.0"))
	float CollapseImpulse = 800.0f;

	/** Angular tumble for natural collapse feel */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Destruction", meta = (ClampMin = "0.0"))
	float DestructionAngularImpulse = 20.0f;

	/** Linear damping on debris — higher = pieces settle faster */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Destruction", meta = (ClampMin = "0.0"))
	float DestructionLinearDamping = 0.5f;

	/** Angular damping on debris — higher = pieces stop spinning sooner */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Destruction", meta = (ClampMin = "0.0"))
	float DestructionAngularDamping = 0.8f;

	/** Max depenetration velocity for GC pieces (cm/s).
	 *  Low value = gentle separation + gravity collapse (like The Finals).
	 *  0 = pieces freeze in place. Default Chaos -1 = explosive. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Destruction", meta = (ClampMin = "0.0"))
	float DestructionMaxDepenetrationVelocity = 10.0f;

	/** Max GC tiles spawned per mesh. Prevents performance disaster on huge meshes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Destruction", meta = (ClampMin = "1"))
	int32 MaxGCTilesPerMesh = 64;

	/** Max objects that get full GC destruction (sorted by volume). Rest just hide. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Destruction", meta = (ClampMin = "1"))
	int32 MaxFullDestructions = 20;

	/** Collision profile for destruction gibs */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Destruction")
	FName DestructionGibCollisionProfile = FName("Ragdoll");

	/** Inner radius — everything inside gets FULL GC tiling (rubble) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Destruction", meta = (ClampMin = "0.0"))
	float DestructionGuaranteedRadius = 500.0f;

	/** Middle radius — between Guaranteed and this value, meshes physically collapse as whole pieces.
	 *  Beyond this radius, survival chance kicks in. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Destruction", meta = (ClampMin = "0.0"))
	float DestructionCollapseRadius = 2000.0f;

	/** Survival chance at the edge of DestructionRadius (0.0 = all destroyed, 1.0 = all survive) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Destruction", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DestructionEdgeSurvivalChance = 0.7f;

	// ==================== Destruction Camera ====================

	/** Min distance (cm) from player to ArenaManager center to allow destruction.
	 *  If player is closer, critical impact is ignored. 0 = always allow. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Destruction", meta = (ClampMin = "0.0", Units = "cm"))
	float MinDestructionDistance = 2000.0f;

	/** Duration (seconds) camera stays locked on epicenter */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Destruction", meta = (ClampMin = "0.0"))
	float CameraLockDuration = 3.0f;

	/** Blend time (seconds) for camera rotation toward epicenter */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Destruction", meta = (ClampMin = "0.0"))
	float CameraBlendTime = 0.3f;

	// ==================== State (Read-Only) ====================

	/** Current arena state */
	UPROPERTY(BlueprintReadOnly, Category = "Arena|State")
	EArenaState CurrentState = EArenaState::Idle;

	/** Current wave index (0-based) */
	UPROPERTY(BlueprintReadOnly, Category = "Arena|State")
	int32 CurrentWaveIndex = -1;

	// ==================== Events ====================

	UPROPERTY(BlueprintAssignable, Category = "Arena|Events")
	FOnArenaStarted OnArenaStarted;

	UPROPERTY(BlueprintAssignable, Category = "Arena|Events")
	FOnArenaCleared OnArenaCleared;

	UPROPERTY(BlueprintAssignable, Category = "Arena|Events")
	FOnWaveStarted OnWaveStarted;

	UPROPERTY(BlueprintAssignable, Category = "Arena|Events")
	FOnWaveCleared OnWaveCleared;

	/** Fired when any object impacts at critical velocity within arena radius */
	UPROPERTY(BlueprintAssignable, Category = "Arena|Events")
	FOnArenaCriticalImpact OnCriticalVelocityImpact;

	/** Fired when all auto-indexed props on the arena are destroyed */
	UPROPERTY(BlueprintAssignable, Category = "Arena|Events")
	FOnAllPropsDestroyed OnAllPropsDestroyed;

	/** Fired after each prop destruction. RemainingPercent: 0.0 (all dead) to 1.0 (all alive). */
	UPROPERTY(BlueprintAssignable, Category = "Arena|Events")
	FOnPropPercentChanged OnPropPercentChanged;

	// ==================== API ====================

	/** Notify arena of a critical velocity impact from any source (prop, projectile, etc).
	 *  Checks distance, broadcasts delegate, triggers destruction if within radius. */
	UFUNCTION(BlueprintCallable, Category = "Arena")
	void NotifyCriticalImpact(AActor* Source, FVector Location, float Speed);

	/** Force-complete the arena: kill all NPCs, skip remaining waves, open exits. */
	UFUNCTION(BlueprintCallable, Category = "Arena")
	void ForceCompleteArena();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:

	// ==================== Activation ====================

	/** Called when player overlaps an entry trigger */
	UFUNCTION()
	void OnEntryTriggerOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	/** Called after delay — player has passed through, now activate */
	void OnActivationDelayFinished();

	/** Activate the arena: close blockers, save checkpoint, start wave 0 */
	void ActivateArena(AShooterCharacter* Player);

	// ==================== Blockers ====================

	/** Enable/disable blockers: visibility + collision (NoCollision when off) */
	void SetBlockersEnabled(bool bEnabled);

	/** Register overlap callbacks on entry trigger actors */
	void RegisterEntryTriggers();

	// ==================== Wave Spawning ====================

	/** Spawn all NPCs for the given wave index */
	void SpawnWave(int32 WaveIndex);

	/** Pick a random spawn point appropriate for the NPC class, avoiding already used points */
	AArenaSpawnPoint* PickSpawnPoint(TSubclassOf<AShooterNPC> NPCClass, const TArray<AArenaSpawnPoint*>& UsedPoints) const;

	/** Called when a spawned NPC dies */
	UFUNCTION()
	void OnNPCDied(AShooterNPC* DeadNPC);

	/** Check if current wave is cleared and advance */
	void CheckWaveComplete();

	/** Timer callback to start the next wave */
	void StartNextWave();

	// ==================== Completion ====================

	/** All waves cleared — open blockers */
	void CompleteArena();

	// ==================== Reset ====================

	/** Full reset: destroy NPCs, hide blockers, return to Idle */
	void ResetArena();

	/** Called when player respawns from checkpoint */
	UFUNCTION()
	void OnPlayerRespawned();

	// ==================== Sustain Mode ====================

	/** Spawn a single enemy from the weighted pool at a LOS-aware spawn point */
	void SpawnSustainEnemy();

	/** Pick spawn point out of player's line of sight, or farthest if all visible.
	 *  Filters out points that exclude the given NPCClass. */
	AArenaSpawnPoint* PickSustainSpawnPoint(TSubclassOf<AShooterNPC> NPCClass);

	/** Pick a random NPC class from SustainEnemyPool using weighted probability */
	TSubclassOf<AShooterNPC> PickWeightedSustainClass() const;

	/** Find and register all ShooterNPCs already placed on the level */
	void RegisterLevelEnemies();

	/** Effective max enemies for sustain mode (resolves MaxSustainEnemies == 0) */
	int32 GetEffectiveMaxSustainEnemies() const;

	/** Number of enemies manually placed on level at BeginPlay */
	int32 InitialLevelEnemyCount = 0;

	/** Remaining respawns allowed. Decremented each time a replacement spawns.
	 *  Negative = infinite (no limit). Initialized from SustainTotalEnemies. */
	int32 SustainRemainingSpawns = -1;

	/** Recently used spawn points (sustain mode). Avoided when picking next point
	 *  to spread enemies across different locations. */
	TArray<AArenaSpawnPoint*> RecentlyUsedSpawnPoints;

	// ==================== NPC Pool (Sustain Mode) ====================

	/** Dead NPCs waiting to be recycled (sustain mode only) */
	UPROPERTY()
	TArray<TWeakObjectPtr<AShooterNPC>> NPCPool;

	/** Try to recycle an NPC of the given class from the pool.
	 *  Returns the recycled NPC (already reset + teleported), or nullptr if no match. */
	AShooterNPC* TryRecycleFromPool(TSubclassOf<AShooterNPC> NPCClass, const FVector& Location, const FRotator& Rotation);

	// ==================== Auto-Indexed Props ====================

	/** Register auto-found props: bind death, cache transforms */
	void RegisterAutoProps();

	/** Called when an auto-indexed prop dies */
	UFUNCTION()
	void OnAutoPropDied(AEMFPhysicsProp* Prop, AActor* Killer);

	/** Respawn all auto-indexed props to their initial state */
	void RespawnAllProps();

	/** Auto-found props */
	UPROPERTY()
	TArray<TWeakObjectPtr<AEMFPhysicsProp>> AutoProps;

	/** Initial transforms of auto-found props (parallel to AutoProps) */
	TArray<FTransform> AutoPropInitialTransforms;

	/** How many auto-props are still alive */
	int32 AliveAutoPropsCount = 0;

	// ==================== Tracked Props ====================

	/** Called when a tracked prop impacts at critical velocity */
	UFUNCTION()
	void OnTrackedPropCriticalImpact(AEMFPhysicsProp* Prop, FVector Location, float Speed);

	/** Called when a projectile impacts at critical velocity */
	UFUNCTION()
	void OnProjectileCriticalImpact(AEMFProjectile* Projectile, FVector Location, float Speed);

	/** Bind to tracked props' OnCriticalVelocityImpact delegates */
	void RegisterTrackedProps();

	// ==================== Arena Destruction ====================

	/** Orchestrate arena-wide destruction shockwave from epicenter */
	void ExecuteArenaDestruction(const FVector& Epicenter);

	/** Spawn GC cube at a static mesh's transform, matching size and orientation */
	void SpawnDestructionGCForMesh(UStaticMeshComponent* MeshComp, const FVector& Epicenter);

	/** Timer handles for staggered destruction wave */
	TArray<FTimerHandle> DestructionTimerHandles;

	/** Prevent double-triggering destruction */
	bool bDestructionExecuted = false;

	// ==================== Camera Lock ====================

	/** Lock camera on destruction epicenter, teleport player if no LOS */
	void StartCameraLock(const FVector& Epicenter);

	/** Restore normal camera control */
	void EndCameraLock();

	/** Per-frame camera rotation interpolation during lock */
	void UpdateCameraLock();

	/** Timer for per-frame camera updates */
	FTimerHandle CameraLockUpdateHandle;

	/** Timer for ending camera lock after duration */
	FTimerHandle CameraLockEndHandle;

	/** Epicenter being looked at during lock */
	FVector CameraLockTarget;

	/** Whether camera is currently locked */
	bool bCameraLocked = false;

	// ==================== Reward Container ====================

	/** Called when the reward dummy dies */
	UFUNCTION()
	void OnRewardDummyDeath(AShooterDummy* Dummy, AActor* Killer);

	// ==================== Island ====================

	/** Called when the linked destructible island is destroyed */
	UFUNCTION()
	void OnLinkedIslandDestroyed(ADestructibleIslandActor* Island, AActor* Destroyer);

	// ==================== Checkpoint ====================

	/** Override checkpoint spawn transform to our respawn point */
	void SaveArenaCheckpoint(AShooterCharacter* Player);

	// ==================== Runtime Data ====================

	/** NPCs currently alive in the active wave */
	UPROPERTY()
	TArray<TWeakObjectPtr<AShooterNPC>> AliveNPCs;

	/** Timer for between-wave delay */
	FTimerHandle WaveTimerHandle;

	/** Timer for activation delay (player entering trigger) */
	FTimerHandle ActivationDelayHandle;

	/** Player who entered trigger, waiting for delay */
	TWeakObjectPtr<AShooterCharacter> PendingPlayer;

	/** Cached reference to checkpoint subsystem */
	UPROPERTY()
	TObjectPtr<UCheckpointSubsystem> CheckpointSubsystem;

	/** Whether we already bound to OnPlayerRespawned delegate */
	bool bBoundToRespawn = false;
};
