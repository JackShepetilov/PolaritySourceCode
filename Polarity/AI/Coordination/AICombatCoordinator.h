// AICombatCoordinator.h
// Global coordinator for NPC attack permissions and role assignment

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AICombatCoordinator.generated.h"

/**
 * Role assigned to NPC for combat coordination
 */
UENUM(BlueprintType)
enum class EAICombatRole : uint8
{
	Attacker,       // Actively attacking, has permission
	Supporter,      // Waiting for attack permission, supporting fire
	Flanker         // Attempting to flank, lower priority for attack
};

/**
 * Internal data for registered NPC
 */
USTRUCT()
struct FRegisteredNPCData
{
	GENERATED_BODY()

	UPROPERTY()
	TWeakObjectPtr<APawn> NPC;

	EAICombatRole Role = EAICombatRole::Supporter;
	float AttackScore = 0.0f;
	float WaitTime = 0.0f;              // Time spent waiting for permission (for queue priority)
	float PermissionTime = 0.0f;        // Time since permission granted (for timeout)
	float AttackingTime = 0.0f;         // Time since attacking started (for stuck detection)
	bool bHasAttackPermission = false;
	bool bIsCurrentlyAttacking = false;
};

/**
 * Singleton coordinator that manages NPC attack permissions.
 * Prevents all NPCs from attacking simultaneously, creating rhythm in combat.
 * Spawn one instance in the level or use GetCoordinator() to auto-spawn.
 */
UCLASS(BlueprintType)
class POLARITY_API AAICombatCoordinator : public AActor
{
	GENERATED_BODY()

public:
	AAICombatCoordinator();

	// ==================== Settings ====================

	/** Maximum number of NPCs that can attack simultaneously */
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

	// ==================== Scoring Weights ====================

	/** Weight of distance in attack score (closer = higher) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Scoring", meta = (ClampMin = "0.0"))
	float DistanceWeight = 1.0f;

	/** Weight of line of sight in attack score */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Scoring", meta = (ClampMin = "0.0"))
	float LineOfSightWeight = 2.0f;

	/** Weight of wait time in attack score (longer wait = higher priority) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Scoring", meta = (ClampMin = "0.0"))
	float WaitTimeWeight = 1.5f;

	/** Maximum distance for attack score calculation (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Scoring", meta = (ClampMin = "100.0"))
	float MaxScoringDistance = 3000.0f;

	// ==================== Engagement Range ====================

	/** Maximum distance from target for NPC to participate in attack coordination (cm).
	 *  NPCs further than this distance are ignored and can attack freely.
	 *  Set to 0 to disable distance check (all NPCs share the same pool). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Range", meta = (ClampMin = "0.0"))
	float MaxEngagementDistance = 2500.0f;

	/** If true, NPCs outside engagement range can attack without permission */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Range")
	bool bAllowFreeAttackOutsideRange = true;

	// ==================== Debug ====================

	/** If true, draw debug visualization for attack coordination */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Debug")
	bool bDrawDebug = false;

	/** Color for NPCs that have attack permission */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Debug")
	FColor DebugColorAttacking = FColor::Red;

	/** Color for NPCs waiting for permission */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Debug")
	FColor DebugColorWaiting = FColor::Yellow;

	/** Color for NPCs outside engagement range */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coordination|Debug")
	FColor DebugColorOutOfRange = FColor::Blue;

	// ==================== API ====================

	/**
	 * Get the combat coordinator instance. Creates one if it doesn't exist.
	 * @param WorldContext - Any UObject in the world
	 * @return The coordinator instance
	 */
	UFUNCTION(BlueprintPure, Category = "Coordination", meta = (WorldContext = "WorldContext"))
	static AAICombatCoordinator* GetCoordinator(const UObject* WorldContext);

	/**
	 * Register an NPC with the coordinator.
	 * @param NPC - The pawn to register
	 */
	UFUNCTION(BlueprintCallable, Category = "Coordination")
	void RegisterNPC(APawn* NPC);

	/**
	 * Unregister an NPC from the coordinator.
	 * @param NPC - The pawn to unregister
	 */
	UFUNCTION(BlueprintCallable, Category = "Coordination")
	void UnregisterNPC(APawn* NPC);

	/**
	 * Request permission to attack.
	 * Permission is granted based on attack score and current attackers.
	 * @param Requester - The pawn requesting permission
	 * @return true if permission granted
	 */
	UFUNCTION(BlueprintCallable, Category = "Coordination")
	bool RequestAttackPermission(APawn* Requester);

	/**
	 * Check if NPC has attack permission without requesting.
	 * @param NPC - The pawn to check
	 * @return true if has permission
	 */
	UFUNCTION(BlueprintPure, Category = "Coordination")
	bool HasAttackPermission(APawn* NPC) const;

	/**
	 * Notify that attack has started (for tracking).
	 * @param Attacker - The attacking pawn
	 */
	UFUNCTION(BlueprintCallable, Category = "Coordination")
	void NotifyAttackStarted(APawn* Attacker);

	/**
	 * Notify that attack has completed. Releases the attack slot.
	 * @param Attacker - The pawn that finished attacking
	 */
	UFUNCTION(BlueprintCallable, Category = "Coordination")
	void NotifyAttackComplete(APawn* Attacker);

	/**
	 * Grant immediate retaliation permission (when NPC is being attacked).
	 * Bypasses normal queue and limits - attacked NPC can always shoot back.
	 * @param NPC - The NPC that was attacked
	 */
	UFUNCTION(BlueprintCallable, Category = "Coordination")
	void GrantRetaliationPermission(APawn* NPC);

	/**
	 * Get the current role of an NPC.
	 * @param NPC - The pawn to query
	 * @return Current combat role
	 */
	UFUNCTION(BlueprintPure, Category = "Coordination")
	EAICombatRole GetNPCRole(APawn* NPC) const;

	/**
	 * Set the role of an NPC.
	 * @param NPC - The pawn to modify
	 * @param NewRole - The new role to assign
	 */
	UFUNCTION(BlueprintCallable, Category = "Coordination")
	void SetNPCRole(APawn* NPC, EAICombatRole NewRole);

	/**
	 * Get the current number of active attackers.
	 */
	UFUNCTION(BlueprintPure, Category = "Coordination")
	int32 GetActiveAttackerCount() const;

	/**
	 * Get the primary target (usually the player).
	 */
	UFUNCTION(BlueprintPure, Category = "Coordination")
	AActor* GetPrimaryTarget() const { return PrimaryTarget.Get(); }

	/**
	 * Set the primary target for all NPCs.
	 * @param Target - The target actor (usually the player)
	 */
	UFUNCTION(BlueprintCallable, Category = "Coordination")
	void SetPrimaryTarget(AActor* Target);

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

	/** Find NPC data by pawn */
	FRegisteredNPCData* FindNPCData(APawn* NPC);
	const FRegisteredNPCData* FindNPCData(APawn* NPC) const;

	/** Update attack scores for all NPCs */
	void UpdateAttackScores();

	/** Calculate attack score for single NPC */
	float CalculateAttackScore(const FRegisteredNPCData& Data) const;

	/** Check line of sight to target */
	bool HasLineOfSightToTarget(APawn* NPC) const;

	/** Clean up invalid NPC references */
	void CleanupInvalidNPCs();

	/** Update permission timeouts */
	void UpdatePermissionTimeouts(float DeltaTime);

	/** Count current attackers */
	int32 CountCurrentAttackers() const;

	/** Check if NPC is within engagement range of target */
	bool IsNPCInEngagementRange(APawn* NPC) const;

	/** Get distance from NPC to primary target */
	float GetDistanceToTarget(APawn* NPC) const;

	/** Draw debug visualization */
	void DrawDebugInfo();
};
