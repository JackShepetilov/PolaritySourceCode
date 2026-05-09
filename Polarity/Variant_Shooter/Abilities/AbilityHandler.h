// AbilityHandler.h
// Runtime logic for one ability instance. Subclass to implement specific abilities or to add
// archetype-shared pipelines (see UAbilityHandler_Burst).
//
// The component does NOT drive a pipeline. It owns inventory + cooldown only and gives the
// handler control on activate. The handler is responsible for its own state machine, animation
// orchestration, and signaling completion via NotifyAbilityComplete / NotifyAbilityCancelled.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "AbilityDefinition.h"
#include "AbilityHandler.generated.h"

class UAbilityComponent;
class AShooterCharacter;
class UAnimMontage;
class USkeletalMeshComponent;
class UAnimInstance;

UCLASS(Blueprintable, Abstract)
class POLARITY_API UAbilityHandler : public UObject
{
	GENERATED_BODY()

public:

	UAbilityHandler();

	// ==================== Lifecycle (called by UAbilityComponent) ====================

	void Initialize(UAbilityComponent* InOwningComponent, UAbilityDefinition* InDefinition, int32 InLevel);
	void SetLevel(int32 NewLevel);

	UFUNCTION(BlueprintNativeEvent, Category = "Ability")
	void OnEquip();
	virtual void OnEquip_Implementation() {}

	UFUNCTION(BlueprintNativeEvent, Category = "Ability")
	void OnUnequip();
	virtual void OnUnequip_Implementation() {}

	UFUNCTION(BlueprintNativeEvent, Category = "Ability")
	void OnLevelChanged(int32 NewLevel);
	virtual void OnLevelChanged_Implementation(int32 NewLevel) {}

	/** Entry point when the player activates the ability via component->TryActivate.
	 *  Handler owns its own pipeline from here. Must eventually call NotifyAbilityComplete
	 *  or NotifyAbilityCancelled. */
	UFUNCTION(BlueprintNativeEvent, Category = "Ability")
	void OnActivate();
	virtual void OnActivate_Implementation() {}

	/** For Hold-mode abilities: called when activation button released. */
	UFUNCTION(BlueprintNativeEvent, Category = "Ability")
	void OnButtonReleased();
	virtual void OnButtonReleased_Implementation() {}

	/** Called by component when activation is cancelled externally (death, weapon swap, slot replace).
	 *  Subclass should clean up its state machine and call NotifyAbilityCancelled when done. */
	UFUNCTION(BlueprintNativeEvent, Category = "Ability")
	void OnCancelRequested();
	virtual void OnCancelRequested_Implementation() {}

	// ==================== Accessors ====================

	UFUNCTION(BlueprintPure, Category = "Ability")
	UAbilityDefinition* GetDefinition() const { return Definition; }

	UFUNCTION(BlueprintPure, Category = "Ability")
	UAbilityComponent* GetOwningComponent() const { return OwningComponent; }

	UFUNCTION(BlueprintPure, Category = "Ability")
	AShooterCharacter* GetOwningCharacter() const { return OwningCharacter; }

	UFUNCTION(BlueprintPure, Category = "Ability")
	int32 GetCurrentLevel() const { return CurrentLevel; }

	UFUNCTION(BlueprintPure, Category = "Ability")
	FAbilityCommonStats GetCommonStats() const;

protected:

	UPROPERTY(BlueprintReadOnly, Category = "Ability")
	TObjectPtr<UAbilityDefinition> Definition;

	UPROPERTY(BlueprintReadOnly, Category = "Ability")
	int32 CurrentLevel = 1;

	UPROPERTY(BlueprintReadOnly, Category = "Ability")
	TObjectPtr<UAbilityComponent> OwningComponent;

	UPROPERTY(BlueprintReadOnly, Category = "Ability")
	TObjectPtr<AShooterCharacter> OwningCharacter;

	// ==================== Helpers (animation) ====================

	/** Cached owner FirstPersonMesh accessor. */
	USkeletalMeshComponent* GetFPMesh() const;
	UAnimInstance* GetFPAnimInstance() const;

	/** Play a montage on FirstPersonMesh. Returns its native length (not effective duration). */
	float PlayFPMontage(UAnimMontage* Montage, float PlayRate = 1.0f, FName StartSection = NAME_None);

	/** Stop a specific montage on FirstPersonMesh with a blend-out time. */
	void StopFPMontage(UAnimMontage* Montage, float BlendOutTime = 0.1f);

	/** Bind end delegate on a specific montage to a UFUNCTION on this handler. */
	void BindFPMontageEnd(UAnimMontage* Montage, FName CallbackFunctionName);

	// ==================== Helpers (charge) ====================

	float GetPlayerChargeModule() const;
	bool TryDeductCharge(float Amount);

	// ==================== Completion API ====================

	/** Signal to component that the ability finished successfully. Triggers cooldown. */
	void NotifyAbilityComplete();

	/** Signal to component that the ability was aborted. No cooldown applied. */
	void NotifyAbilityCancelled();
};
