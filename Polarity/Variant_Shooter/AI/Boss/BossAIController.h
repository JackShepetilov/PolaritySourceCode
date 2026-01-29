// BossAIController.h
// AI Controller for the hybrid boss character

#pragma once

#include "CoreMinimal.h"
#include "Variant_Shooter/AI/ShooterAIController.h"
#include "BossAIController.generated.h"

class ABossCharacter;
class UFlyingAIMovementComponent;

/**
 * AI Controller for the Boss character.
 * Extends ShooterAIController with boss-specific commands for both ground and aerial phases.
 */
UCLASS()
class POLARITY_API ABossAIController : public AShooterAIController
{
	GENERATED_BODY()

public:

	ABossAIController();

protected:

	// ==================== Cached References ====================

	/** Cached pointer to the controlled boss */
	UPROPERTY()
	TObjectPtr<ABossCharacter> ControlledBoss;

	/** Cached pointer to flying movement component */
	UPROPERTY()
	TObjectPtr<UFlyingAIMovementComponent> FlyingMovement;

protected:

	// ==================== Controller Lifecycle ====================

	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;

public:

	// ==================== Ground Phase Commands ====================

	/**
	 * Start arc dash towards target
	 * @param Target - Actor to dash towards
	 * @return True if dash started successfully
	 */
	UFUNCTION(BlueprintCallable, Category = "Boss AI|Ground")
	bool StartArcDash(AActor* Target);

	/**
	 * Start melee attack against target
	 * @param Target - Actor to attack
	 */
	UFUNCTION(BlueprintCallable, Category = "Boss AI|Ground")
	void StartMeleeAttack(AActor* Target);

	/**
	 * Check if target is in melee range
	 * @param Target - Actor to check
	 * @return True if in melee range
	 */
	UFUNCTION(BlueprintPure, Category = "Boss AI|Ground")
	bool IsTargetInMeleeRange(AActor* Target) const;

	// ==================== Aerial Phase Commands ====================

	/**
	 * Command boss to start hovering (enter aerial mode)
	 */
	UFUNCTION(BlueprintCallable, Category = "Boss AI|Aerial")
	void StartHovering();

	/**
	 * Command boss to stop hovering (return to ground)
	 */
	UFUNCTION(BlueprintCallable, Category = "Boss AI|Aerial")
	void StopHovering();

	/**
	 * Perform aerial strafe movement
	 * @param Direction - Direction to strafe
	 */
	UFUNCTION(BlueprintCallable, Category = "Boss AI|Aerial")
	void AerialStrafe(const FVector& Direction);

	/**
	 * Perform evasive aerial dash
	 * @return True if dash started successfully
	 */
	UFUNCTION(BlueprintCallable, Category = "Boss AI|Aerial")
	bool PerformAerialDash();

	/**
	 * Fire EMF projectile at target
	 * @param Target - Actor to shoot at
	 */
	UFUNCTION(BlueprintCallable, Category = "Boss AI|Aerial")
	void FireEMFProjectile(AActor* Target);

	/**
	 * Match opposite polarity of target (so projectile attracts to them)
	 * @param Target - Actor whose polarity to oppose
	 */
	UFUNCTION(BlueprintCallable, Category = "Boss AI|Aerial")
	void MatchOppositePolarity(AActor* Target);

	// ==================== Phase Management ====================

	/**
	 * Set boss phase
	 * @param NewPhase - Phase to transition to
	 */
	UFUNCTION(BlueprintCallable, Category = "Boss AI|Phase")
	void SetPhase(EBossPhase NewPhase);

	/**
	 * Get current boss phase
	 * @return Current phase
	 */
	UFUNCTION(BlueprintPure, Category = "Boss AI|Phase")
	EBossPhase GetCurrentPhase() const;

	/**
	 * Check if boss should transition to aerial phase
	 * @return True if should transition
	 */
	UFUNCTION(BlueprintPure, Category = "Boss AI|Phase")
	bool ShouldTransitionToAerial() const;

	/**
	 * Check if boss should transition to ground phase
	 * @return True if should transition
	 */
	UFUNCTION(BlueprintPure, Category = "Boss AI|Phase")
	bool ShouldTransitionToGround() const;

	// ==================== Finisher Phase ====================

	/**
	 * Enter finisher phase (boss at 1 HP)
	 */
	UFUNCTION(BlueprintCallable, Category = "Boss AI|Finisher")
	void EnterFinisherPhase();

	/**
	 * Check if boss is in finisher phase
	 * @return True if in finisher phase
	 */
	UFUNCTION(BlueprintPure, Category = "Boss AI|Finisher")
	bool IsInFinisherPhase() const;

	// ==================== State Queries ====================

	/** Returns true if boss is currently dashing */
	UFUNCTION(BlueprintPure, Category = "Boss AI|State")
	bool IsDashing() const;

	/** Returns true if boss can currently dash */
	UFUNCTION(BlueprintPure, Category = "Boss AI|State")
	bool CanDash() const;

	/** Returns true if boss can currently melee attack */
	UFUNCTION(BlueprintPure, Category = "Boss AI|State")
	bool CanMeleeAttack() const;

	/** Get the controlled boss */
	UFUNCTION(BlueprintPure, Category = "Boss AI")
	ABossCharacter* GetControlledBoss() const { return ControlledBoss; }

	/** Get the flying movement component */
	UFUNCTION(BlueprintPure, Category = "Boss AI")
	UFlyingAIMovementComponent* GetFlyingMovement() const { return FlyingMovement; }
};
