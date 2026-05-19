// BossAIController.h
// AI Controller for the ground-melee boss character

#pragma once

#include "CoreMinimal.h"
#include "Variant_Shooter/AI/ShooterAIController.h"
#include "BossAIController.generated.h"

class ABossCharacter;

/**
 * AI Controller for the Boss character.
 * Extends ShooterAIController with boss-specific commands for ground melee phase.
 */
UCLASS()
class POLARITY_API ABossAIController : public AShooterAIController
{
	GENERATED_BODY()

public:
	ABossAIController(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
	// ==================== Cached References ====================

	UPROPERTY()
	TObjectPtr<ABossCharacter> ControlledBoss;

protected:
	// ==================== Controller Lifecycle ====================

	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;

public:
	// ==================== Ground Phase Commands ====================

	UFUNCTION(BlueprintCallable, Category = "Boss AI|Ground")
	bool StartApproachDash(AActor* Target);

	UFUNCTION(BlueprintCallable, Category = "Boss AI|Ground")
	bool StartCircleDash(AActor* Target);

	UFUNCTION(BlueprintCallable, Category = "Boss AI|Ground")
	void StartMeleeAttack(AActor* Target);

	UFUNCTION(BlueprintPure, Category = "Boss AI|Ground")
	bool IsTargetInMeleeRange(AActor* Target) const;

	// ==================== Phase Management ====================

	UFUNCTION(BlueprintCallable, Category = "Boss AI|Phase")
	void SetPhase(EBossPhase NewPhase);

	UFUNCTION(BlueprintPure, Category = "Boss AI|Phase")
	EBossPhase GetCurrentPhase() const;

	// ==================== Finisher Phase ====================

	UFUNCTION(BlueprintCallable, Category = "Boss AI|Finisher")
	void EnterFinisherPhase();

	UFUNCTION(BlueprintPure, Category = "Boss AI|Finisher")
	bool IsInFinisherPhase() const;

	// ==================== State Queries ====================

	UFUNCTION(BlueprintPure, Category = "Boss AI|State")
	bool IsDashing() const;

	UFUNCTION(BlueprintPure, Category = "Boss AI|State")
	bool CanDash() const;

	UFUNCTION(BlueprintPure, Category = "Boss AI|State")
	bool CanMeleeAttack() const;

	UFUNCTION(BlueprintPure, Category = "Boss AI")
	ABossCharacter* GetControlledBoss() const { return ControlledBoss; }
};
