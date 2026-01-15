// MeleeRetreatComponent.h
// NPC retreat behavior based on proximity to player

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MeleeRetreatComponent.generated.h"

class ACharacter;
class UCharacterMovementComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRetreatStarted, const FVector&, RetreatDirection);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnRetreatEnded);

/**
 * Component that handles NPC retreat behavior based on proximity to player.
 * When NPC stays too close to the target for too long, it will retreat.
 */
UCLASS(ClassGroup = (AI), meta = (BlueprintSpawnableComponent))
class POLARITY_API UMeleeRetreatComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMeleeRetreatComponent();

	// ==================== Settings ====================

	/** Distance to retreat from attacker (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retreat", meta = (ClampMin = "100.0", ClampMax = "1000.0"))
	float RetreatDistance = 500.0f;

	/** Duration of retreat state (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retreat", meta = (ClampMin = "0.5", ClampMax = "5.0"))
	float RetreatDuration = 2.0f;

	/** Cooldown before another retreat can be triggered (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retreat", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float RetreatCooldown = 5.0f;

	/** Movement speed multiplier during retreat */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retreat", meta = (ClampMin = "0.5", ClampMax = "2.0"))
	float RetreatSpeedMultiplier = 1.3f;

	/** If true, NPC will not attack during retreat */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retreat")
	bool bDisableAttackDuringRetreat = true;

	/** Minimum angle deviation when direct retreat path is blocked (degrees) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retreat|Pathfinding", meta = (ClampMin = "15.0", ClampMax = "90.0"))
	float PathDeviationAngle = 30.0f;

	/** Number of alternative directions to try if direct retreat is blocked */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retreat|Pathfinding", meta = (ClampMin = "2", ClampMax = "8"))
	int32 AlternativeDirectionCount = 4;

	// ==================== Proximity Trigger ====================

	/** If true, retreat triggers automatically when staying close to target */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retreat|Proximity")
	bool bEnableProximityTrigger = true;

	/** Distance threshold for proximity trigger (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retreat|Proximity", meta = (ClampMin = "100.0", ClampMax = "500.0", EditCondition = "bEnableProximityTrigger"))
	float ProximityTriggerDistance = 250.0f;

	/** Time NPC must stay within proximity distance to trigger retreat (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retreat|Proximity", meta = (ClampMin = "0.5", ClampMax = "5.0", EditCondition = "bEnableProximityTrigger"))
	float ProximityTriggerTime = 1.5f;

	// ==================== Events ====================

	/** Called when retreat starts */
	UPROPERTY(BlueprintAssignable, Category = "Retreat|Events")
	FOnRetreatStarted OnRetreatStarted;

	/** Called when retreat ends */
	UPROPERTY(BlueprintAssignable, Category = "Retreat|Events")
	FOnRetreatEnded OnRetreatEnded;

	// ==================== Runtime State ====================

	/** Is currently retreating */
	UPROPERTY(BlueprintReadOnly, Category = "Retreat|State")
	bool bIsRetreating = false;

	/** Current retreat direction (world space) */
	UPROPERTY(BlueprintReadOnly, Category = "Retreat|State")
	FVector RetreatDirection = FVector::ZeroVector;

	/** Retreat destination point */
	UPROPERTY(BlueprintReadOnly, Category = "Retreat|State")
	FVector RetreatDestination = FVector::ZeroVector;

	// ==================== API ====================

	/**
	 * Trigger retreat from an attacker.
	 * @param Attacker - The actor that hit this NPC
	 * @return true if retreat was triggered, false if on cooldown
	 */
	UFUNCTION(BlueprintCallable, Category = "Retreat")
	bool TriggerRetreat(AActor* Attacker);

	/**
	 * Force end the current retreat.
	 */
	UFUNCTION(BlueprintCallable, Category = "Retreat")
	void EndRetreat();

	/**
	 * Check if retreat can be triggered (not on cooldown).
	 */
	UFUNCTION(BlueprintPure, Category = "Retreat")
	bool CanRetreat() const;

	/**
	 * Check if currently retreating.
	 */
	UFUNCTION(BlueprintPure, Category = "Retreat")
	bool IsRetreating() const { return bIsRetreating; }

	/**
	 * Get the retreat destination point.
	 */
	UFUNCTION(BlueprintPure, Category = "Retreat")
	FVector GetRetreatDestination() const { return RetreatDestination; }

	/**
	 * Get time remaining in retreat state.
	 */
	UFUNCTION(BlueprintPure, Category = "Retreat")
	float GetRetreatTimeRemaining() const { return RetreatTimeRemaining; }

	/**
	 * Get cooldown time remaining.
	 */
	UFUNCTION(BlueprintPure, Category = "Retreat")
	float GetCooldownRemaining() const { return CooldownRemaining; }

	/**
	 * Get the last attacker.
	 */
	UFUNCTION(BlueprintPure, Category = "Retreat")
	AActor* GetLastAttacker() const { return LastAttacker.Get(); }

	/**
	 * Set the target actor for proximity checks.
	 * @param Target - The actor to track (usually the player)
	 */
	UFUNCTION(BlueprintCallable, Category = "Retreat|Proximity")
	void SetProximityTarget(AActor* Target);

	/**
	 * Get accumulated proximity time.
	 */
	UFUNCTION(BlueprintPure, Category = "Retreat|Proximity")
	float GetProximityTimeAccumulated() const { return ProximityTimeAccumulated; }

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	/** Time remaining in retreat state */
	float RetreatTimeRemaining = 0.0f;

	/** Cooldown time remaining */
	float CooldownRemaining = 0.0f;

	/** Original max walk speed (to restore after retreat) */
	float OriginalMaxWalkSpeed = 0.0f;

	/** Last attacker reference */
	TWeakObjectPtr<AActor> LastAttacker;

	/** Target actor for proximity checks */
	TWeakObjectPtr<AActor> ProximityTarget;

	/** Accumulated time within proximity distance */
	float ProximityTimeAccumulated = 0.0f;

	/** Cached owner character */
	UPROPERTY()
	TObjectPtr<ACharacter> OwnerCharacter;

	/** Cached movement component */
	UPROPERTY()
	TObjectPtr<UCharacterMovementComponent> MovementComponent;

	/** Calculate retreat direction away from attacker */
	FVector CalculateRetreatDirection(AActor* Attacker) const;

	/** Find valid retreat destination using navmesh */
	FVector FindRetreatDestination(const FVector& Direction) const;

	/** Check if a point is reachable on navmesh */
	bool IsPointReachable(const FVector& Point) const;

	/** Apply speed multiplier */
	void ApplyRetreatSpeed();

	/** Restore original speed */
	void RestoreOriginalSpeed();

	/** Update proximity trigger logic */
	void UpdateProximityTrigger(float DeltaTime);

	/** Find player if no proximity target set */
	void FindProximityTarget();
};
