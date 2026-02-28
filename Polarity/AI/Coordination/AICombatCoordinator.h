// AICombatCoordinator.h
// Global coordinator for NPC attack permissions, token-based combat, battle circle positioning, and role/pressure management

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AICombatCoordinator.generated.h"

// ==================== Enums ====================

/** Type of attack token (determines which pool the NPC draws from) */
UENUM(BlueprintType)
enum class EAttackTokenType : uint8
{
	Ranged,     // ShooterNPC burst fire, FlyingDrone shooting
	Melee,      // MeleeNPC dash + melee attack
	Special     // Reserved for boss abilities, grenades, etc.
};

/** Role assigned to NPC for combat coordination */
UENUM(BlueprintType)
enum class EAICombatRole : uint8
{
	Aggressor,      // Actively pushing player, inner ring, always attacks
	Supporter,      // Mid-range fire support, middle ring
	Flanker,        // Positioned >90 degrees from player facing
	Pressurer       // Responds to player state (low HP, no armor)
};

/** Ring definition for battle circle positioning */
UENUM(BlueprintType)
enum class EBattleRing : uint8
{
	Inner,    // 400-600cm, melee/aggressive
	Middle,   // 600-1200cm, shooters
	Outer     // 1200-2000cm, drones/snipers
};

// ==================== Structs ====================

/** Token pool for a specific attack type */
USTRUCT()
struct FTokenPool
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0", ClampMax = "10"))
	int32 MaxTokens = 2;

	TArray<TWeakObjectPtr<APawn>> HeldBy;

	int32 GetAvailableCount() const { return FMath::Max(0, MaxTokens - HeldBy.Num()); }
	bool HasToken(APawn* NPC) const;
	bool TryAcquire(APawn* NPC);
	void Release(APawn* NPC);
	void CleanupInvalid();
};

/** Battle circle slot — a position around the player that an NPC is assigned to */
USTRUCT()
struct FBattleSlot
{
	GENERATED_BODY()

	FVector WorldPosition = FVector::ZeroVector;
	float AngleDeg = 0.0f;
	EBattleRing Ring = EBattleRing::Middle;
	TWeakObjectPtr<APawn> AssignedNPC;

	bool IsOccupied() const { return AssignedNPC.IsValid(); }
};

/** Internal data for registered NPC */
USTRUCT()
struct FRegisteredNPCData
{
	GENERATED_BODY()

	UPROPERTY()
	TWeakObjectPtr<APawn> NPC;

	EAICombatRole Role = EAICombatRole::Supporter;
	float AttackScore = 0.0f;
	float WaitTime = 0.0f;
	float PermissionTime = 0.0f;
	float AttackingTime = 0.0f;
	bool bHasAttackPermission = false;
	bool bIsCurrentlyAttacking = false;

	// Token system
	EAttackTokenType TokenType = EAttackTokenType::Ranged;
	bool bHasToken = false;
	bool bProximityOverride = false;

	// Battle Circle
	int32 AssignedSlotIndex = -1;
	FVector AssignedSlotPosition = FVector::ZeroVector;

	// Role/Pressure
	float AngleToPlayerFacing = 0.0f;
};

/** Cached player state for pressure system */
struct FPlayerStateCache
{
	float HPPercent = 1.0f;
	float ArmorPercent = 0.0f;
	float Speed = 0.0f;
	FVector FacingDirection = FVector::ForwardVector;
	FVector Position = FVector::ZeroVector;
	bool bIsValid = false;
};

// ==================== Coordinator ====================

/**
 * Singleton coordinator that manages NPC combat behavior:
 * - Token-based attack permissions (Ranged/Melee/Special pools)
 * - Battle circle positioning (slot-based rings around player)
 * - Role & pressure management (dynamic roles based on player state)
 * Spawn one instance in the level or use GetCoordinator() to auto-spawn.
 */
UCLASS(BlueprintType)
class POLARITY_API AAICombatCoordinator : public AActor
{
	GENERATED_BODY()

public:
	AAICombatCoordinator();

	// ==================== General Settings ====================

	/** Maximum number of NPCs that can attack simultaneously (legacy, still enforced as total cap) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination", meta = (ClampMin = "1", ClampMax = "10"))
	int32 MaxSimultaneousAttackers = 3;

	/** Minimum time between attack permission grants (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float MinTimeBetweenAttacks = 0.1f;

	/** Time before attack permission expires if not used (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination", meta = (ClampMin = "1.0", ClampMax = "10.0"))
	float AttackPermissionTimeout = 2.0f;

	/** Maximum time an NPC can hold "attacking" status before being reset (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination", meta = (ClampMin = "1.0", ClampMax = "30.0"))
	float MaxAttackingTime = 10.0f;

	// ==================== Token System ====================

	/** Maximum simultaneous ranged attack tokens */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Tokens", meta = (ClampMin = "0", ClampMax = "10"))
	int32 MaxRangedTokens = 2;

	/** Maximum simultaneous melee attack tokens */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Tokens", meta = (ClampMin = "0", ClampMax = "10"))
	int32 MaxMeleeTokens = 1;

	/** Maximum simultaneous special attack tokens */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Tokens", meta = (ClampMin = "0", ClampMax = "10"))
	int32 MaxSpecialTokens = 1;

	/** Distance threshold for proximity override (cm). NPC within this range attacks without token. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Tokens", meta = (ClampMin = "0"))
	float ProximityOverrideDistance = 250.0f;

	/** If true, NPC with LOS can steal token from NPC without LOS who is farther */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Tokens")
	bool bAllowTokenStealing = true;

	// ==================== Scoring Weights ====================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Scoring", meta = (ClampMin = "0.0"))
	float DistanceWeight = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Scoring", meta = (ClampMin = "0.0"))
	float LineOfSightWeight = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Scoring", meta = (ClampMin = "0.0"))
	float WaitTimeWeight = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Scoring", meta = (ClampMin = "100.0"))
	float MaxScoringDistance = 3000.0f;

	// ==================== Engagement Range ====================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Range", meta = (ClampMin = "0.0"))
	float MaxEngagementDistance = 2500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Range")
	bool bAllowFreeAttackOutsideRange = true;

	// ==================== Battle Circle ====================

	/** If true, use battle circle positioning instead of random NavMesh */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|BattleCircle")
	bool bUseBattleCircle = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|BattleCircle", meta = (ClampMin = "100"))
	float InnerRingMinRadius = 400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|BattleCircle", meta = (ClampMin = "100"))
	float InnerRingMaxRadius = 600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|BattleCircle", meta = (ClampMin = "100"))
	float MiddleRingMinRadius = 600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|BattleCircle", meta = (ClampMin = "100"))
	float MiddleRingMaxRadius = 1200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|BattleCircle", meta = (ClampMin = "100"))
	float OuterRingMinRadius = 1200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|BattleCircle", meta = (ClampMin = "100"))
	float OuterRingMaxRadius = 2000.0f;

	/** How often to recalculate slot world positions (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|BattleCircle", meta = (ClampMin = "0.1"))
	float SlotRecalculationInterval = 0.5f;

	// ==================== Role & Pressure ====================

	/** HP percentage threshold below which pressure tactics activate */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Pressure", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LowHPThreshold = 0.3f;

	/** Armor percentage threshold below which grouping tactics activate */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Pressure", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LowArmorThreshold = 0.1f;

	/** Minimum angle from player facing direction to qualify as Flanker (degrees) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Pressure", meta = (ClampMin = "45", ClampMax = "180"))
	float FlankerMinAngle = 90.0f;

	// ==================== Debug ====================

	/** Draw token/attacker status debug info */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Debug")
	bool bDrawDebug = false;

	/** Draw battle circle rings and slots */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Debug")
	bool bDrawBattleCircle = false;

	/** Draw role names, player facing, pressure status */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Debug")
	bool bDrawRoleDebug = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Debug")
	FColor DebugColorAttacking = FColor::Red;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Debug")
	FColor DebugColorWaiting = FColor::Yellow;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Debug")
	FColor DebugColorOutOfRange = FColor::Blue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Debug")
	FColor DebugColorInnerRing = FColor(255, 100, 100);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Debug")
	FColor DebugColorMiddleRing = FColor(100, 255, 100);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Debug")
	FColor DebugColorOuterRing = FColor(100, 100, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Debug")
	FColor DebugColorAggressor = FColor::Red;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Debug")
	FColor DebugColorFlanker = FColor::Magenta;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Debug")
	FColor DebugColorPressurer = FColor(255, 128, 0);

	// ==================== API ====================

	/** Get the combat coordinator instance. Creates one if it doesn't exist. */
	UFUNCTION(BlueprintPure, Category = "Coordination", meta = (WorldContext = "WorldContext"))
	static AAICombatCoordinator* GetCoordinator(const UObject* WorldContext);

	/** Register an NPC with the coordinator. */
	UFUNCTION(BlueprintCallable, Category = "Coordination")
	void RegisterNPC(APawn* NPC);

	/** Unregister an NPC from the coordinator. */
	UFUNCTION(BlueprintCallable, Category = "Coordination")
	void UnregisterNPC(APawn* NPC);

	/** Request permission to attack (bridges to token system internally). */
	UFUNCTION(BlueprintCallable, Category = "Coordination")
	bool RequestAttackPermission(APawn* Requester);

	/** Check if NPC has attack permission without requesting. */
	UFUNCTION(BlueprintPure, Category = "Coordination")
	bool HasAttackPermission(APawn* NPC) const;

	/** Notify that attack has started (for tracking). */
	UFUNCTION(BlueprintCallable, Category = "Coordination")
	void NotifyAttackStarted(APawn* Attacker);

	/** Notify that attack has completed. Releases attack token. */
	UFUNCTION(BlueprintCallable, Category = "Coordination")
	void NotifyAttackComplete(APawn* Attacker);

	/** Grant immediate retaliation permission (bypasses tokens). */
	UFUNCTION(BlueprintCallable, Category = "Coordination")
	void GrantRetaliationPermission(APawn* NPC);

	/** Get the current role of an NPC. */
	UFUNCTION(BlueprintPure, Category = "Coordination")
	EAICombatRole GetNPCRole(APawn* NPC) const;

	/** Set the role of an NPC. */
	UFUNCTION(BlueprintCallable, Category = "Coordination")
	void SetNPCRole(APawn* NPC, EAICombatRole NewRole);

	/** Get the current number of active attackers. */
	UFUNCTION(BlueprintPure, Category = "Coordination")
	int32 GetActiveAttackerCount() const;

	/** Get the primary target (usually the player). */
	UFUNCTION(BlueprintPure, Category = "Coordination")
	AActor* GetPrimaryTarget() const { return PrimaryTarget.Get(); }

	/** Set the primary target for all NPCs. */
	UFUNCTION(BlueprintCallable, Category = "Coordination")
	void SetPrimaryTarget(AActor* Target);

	// --- Token API ---

	/** Request a typed attack token. Returns true if token acquired or proximity override active. */
	UFUNCTION(BlueprintCallable, Category = "Coordination|Tokens")
	bool RequestAttackToken(APawn* Requester, EAttackTokenType TokenType);

	/** Release a held attack token. */
	UFUNCTION(BlueprintCallable, Category = "Coordination|Tokens")
	void ReleaseAttackToken(APawn* Attacker);

	/** Check if NPC has a token or proximity override. */
	UFUNCTION(BlueprintPure, Category = "Coordination|Tokens")
	bool HasAttackToken(APawn* NPC) const;

	// --- Battle Circle API ---

	/** Get the assigned slot position for an NPC. Returns false if no slot assigned. */
	UFUNCTION(BlueprintPure, Category = "Coordination|BattleCircle")
	bool GetAssignedSlotPosition(APawn* NPC, FVector& OutPosition) const;

	/** Get the ring assignment for an NPC. */
	UFUNCTION(BlueprintPure, Category = "Coordination|BattleCircle")
	EBattleRing GetNPCRing(APawn* NPC) const;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

private:
	/** Registered NPCs */
	UPROPERTY()
	TArray<FRegisteredNPCData> RegisteredNPCs;

	/** Primary target (player) */
	TWeakObjectPtr<AActor> PrimaryTarget;

	/** Time since last attack permission was granted */
	float TimeSinceLastAttackGrant = 0.0f;

	/** Singleton instance */
	static TWeakObjectPtr<AAICombatCoordinator> Instance;

	// --- Core helpers ---
	FRegisteredNPCData* FindNPCData(APawn* NPC);
	const FRegisteredNPCData* FindNPCData(APawn* NPC) const;
	void UpdateAttackScores();
	float CalculateAttackScore(const FRegisteredNPCData& Data) const;
	bool HasLineOfSightToTarget(APawn* NPC) const;
	void CleanupInvalidNPCs();
	void UpdatePermissionTimeouts(float DeltaTime);
	int32 CountCurrentAttackers() const;
	bool IsNPCInEngagementRange(APawn* NPC) const;
	float GetDistanceToTarget(APawn* NPC) const;

	// --- Token system ---
	FTokenPool RangedTokenPool;
	FTokenPool MeleeTokenPool;
	FTokenPool SpecialTokenPool;

	FTokenPool& GetPoolForType(EAttackTokenType Type);
	const FTokenPool& GetPoolForType(EAttackTokenType Type) const;
	EAttackTokenType DetermineTokenType(APawn* NPC) const;
	bool TryStealToken(APawn* Requester, FTokenPool& Pool);
	void UpdateProximityOverrides();
	void UpdateTokenPools();

	// --- Battle Circle ---
	TArray<FBattleSlot> BattleSlots;
	float TimeSinceLastSlotRecalc = 0.0f;
	FVector LastSlotCalcPlayerPosition = FVector::ZeroVector;
	int32 LastSlotNPCCount = 0;

	void GenerateBattleSlots();
	void RecalculateSlotPositions();
	void AssignNPCsToSlots();
	EBattleRing GetPreferredRing(const FRegisteredNPCData& Data) const;
	float GetRingMidRadius(EBattleRing Ring) const;

	// --- Role & Pressure ---
	FPlayerStateCache CachedPlayerState;

	void UpdatePlayerStateCache();
	void AssignRoles();
	float CalculateAngleFromPlayerFacing(APawn* NPC) const;

	// --- Debug ---
	void DrawDebugInfo();
	void DrawBattleCircleDebug();
	void DrawRoleDebug();
};
