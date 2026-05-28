// BossAIController.h
// AI Controller for the boss character.

#pragma once

#include "CoreMinimal.h"
#include "Variant_Shooter/AI/ShooterAIController.h"
#include "BossCharacter.h"
#include "BossAIController.generated.h"

/**
 * AI Controller for the Boss character.
 * Extends ShooterAIController with boss phase/finisher convenience wrappers. Combat actions
 * (melee, shoot, strafe) are driven directly on ABossCharacter by the boss StateTree tasks.
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

	// ==================== Controller Lifecycle ====================

	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;

public:
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

	UFUNCTION(BlueprintPure, Category = "Boss AI")
	ABossCharacter* GetControlledBoss() const { return ControlledBoss; }
};
