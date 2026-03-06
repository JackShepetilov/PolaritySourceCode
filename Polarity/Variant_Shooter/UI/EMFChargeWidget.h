// EMFChargeWidget.h
// Widget that displays EMF charge above an actor's head (NPC or Physics Prop)

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "EMFChargeWidget.generated.h"

class AShooterNPC;
class AEMFPhysicsProp;
class ADroppedMeleeWeapon;

/**
 * Base class for EMF charge indicator widget displayed above NPCs and Props.
 * Inherit in Blueprint to create the visual representation (progress bar, text, etc.).
 * Position is updated by EMFChargeWidgetSubsystem (not NativeTick) to avoid
 * Slate paint dependency where Hidden widgets stop ticking.
 */
UCLASS(Abstract, Blueprintable)
class POLARITY_API UEMFChargeWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Bind this widget to an NPC */
	void BindToNPC(AShooterNPC* InNPC, float InVerticalOffset = 120.0f);

	/** Bind this widget to a physics prop */
	void BindToProp(AEMFPhysicsProp* InProp, float InVerticalOffset = 50.0f);

	/** Bind this widget to a dropped melee weapon */
	void BindToDroppedWeapon(ADroppedMeleeWeapon* InWeapon, float InVerticalOffset = 30.0f);

	/** Unbind from current target and deactivate */
	void Unbind();

	/** Reset for pool reuse */
	void ResetWidget();

	/**
	 * Update screen position based on target world location.
	 * Called every frame by EMFChargeWidgetSubsystem::Tick.
	 */
	void UpdateScreenPosition(APlayerController* PC);

	// ==================== Blueprint Events ====================

	/** Called when charge value changes. Implement in Blueprint to update visuals. */
	UFUNCTION(BlueprintImplementableEvent, Category = "EMF Charge",
		meta = (DisplayName = "On Charge Updated"))
	void BP_OnChargeUpdated(float InChargeValue, uint8 InPolarity, float InNormalizedCharge);

	/** Called when widget is first bound to a target */
	UFUNCTION(BlueprintImplementableEvent, Category = "EMF Charge",
		meta = (DisplayName = "On Bound To NPC"))
	void BP_OnBoundToNPC();

	/** Called when widget is reset for pool reuse */
	UFUNCTION(BlueprintImplementableEvent, Category = "EMF Charge",
		meta = (DisplayName = "On Widget Reset"))
	void BP_OnWidgetReset();

	/** Called when NPC enters stun state (explosion stun, knockback, etc.) */
	UFUNCTION(BlueprintImplementableEvent, Category = "EMF Charge",
		meta = (DisplayName = "On Stun Start"))
	void BP_OnStunStart(float Duration);

	/** Called when NPC exits stun state */
	UFUNCTION(BlueprintImplementableEvent, Category = "EMF Charge",
		meta = (DisplayName = "On Stun End"))
	void BP_OnStunEnd();

	/** Called when NPC health changes. Use for HP bar display. */
	UFUNCTION(BlueprintImplementableEvent, Category = "EMF Charge",
		meta = (DisplayName = "On Health Changed"))
	void BP_OnHealthChanged(float CurrentHP, float MaxHP, float NormalizedHP);

	// ==================== Getters ====================

	UFUNCTION(BlueprintPure, Category = "EMF Charge")
	bool IsActive() const { return bIsActive; }

	UFUNCTION(BlueprintPure, Category = "EMF Charge")
	AActor* GetBoundActor() const;

	UFUNCTION(BlueprintPure, Category = "EMF Charge")
	float GetCurrentCharge() const { return CurrentCharge; }

	UFUNCTION(BlueprintPure, Category = "EMF Charge")
	uint8 GetCurrentPolarity() const { return CurrentPolarity; }

	/** Widget half-size for centering (pixels) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "EMF Charge|Layout")
	FVector2D WidgetHalfSize = FVector2D(40.0f, 10.0f);

	/** Enable distance-based scaling (widget shrinks with distance) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "EMF Charge|Layout")
	bool bEnableDistanceScaling = true;

	/** Distance closer than this = MaxWidgetScale (cm) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "EMF Charge|Layout",
		meta = (EditCondition = "bEnableDistanceScaling", ClampMin = "100"))
	float MaxScaleDistance = 500.0f;

	/** Distance farther than this = MinWidgetScale (cm) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "EMF Charge|Layout",
		meta = (EditCondition = "bEnableDistanceScaling", ClampMin = "100"))
	float MinScaleDistance = 3000.0f;

	/** Scale when very close (<= MaxScaleDistance) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "EMF Charge|Layout",
		meta = (EditCondition = "bEnableDistanceScaling", ClampMin = "0.1", ClampMax = "5.0"))
	float MaxWidgetScale = 1.0f;

	/** Scale when far away (>= MinScaleDistance) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "EMF Charge|Layout",
		meta = (EditCondition = "bEnableDistanceScaling", ClampMin = "0.1", ClampMax = "5.0"))
	float MinWidgetScale = 0.3f;

	/** Hide widget when target is behind a wall */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "EMF Charge|Layout")
	bool bOcclusionCheck = true;

protected:
	/** The NPC this widget is tracking (mutually exclusive with BoundProp) */
	TWeakObjectPtr<AShooterNPC> BoundNPC;

	/** The prop this widget is tracking (mutually exclusive with BoundNPC) */
	TWeakObjectPtr<AEMFPhysicsProp> BoundProp;

	/** The dropped weapon this widget is tracking */
	TWeakObjectPtr<ADroppedMeleeWeapon> BoundDroppedWeapon;

	UPROPERTY(BlueprintReadOnly, Category = "EMF Charge")
	bool bIsActive = false;

	UPROPERTY(BlueprintReadOnly, Category = "EMF Charge")
	float CurrentCharge = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "EMF Charge")
	uint8 CurrentPolarity = 0;

	UPROPERTY(BlueprintReadOnly, Category = "EMF Charge")
	float NormalizedCharge = 0.0f;

	float VerticalOffset = 120.0f;

	/** Max charge for normalization (cached on bind) */
	float CachedMaxCharge = 50.0f;

	/** Max HP for normalization (cached on bind from NPC's initial CurrentHP) */
	float CachedMaxHP = 100.0f;

private:
	/** Get target world position (above head/top) */
	bool GetTargetWorldPosition(FVector& OutPosition) const;

	/** Is the target dead? */
	bool IsTargetDead() const;

	UFUNCTION()
	void OnNPCChargeUpdated(float InChargeValue, uint8 InPolarity);

	UFUNCTION()
	void OnPropChargeUpdated(float InNewCharge, uint8 InNewPolarity);

	/** Shared charge update logic */
	void HandleChargeUpdate(float InChargeValue, uint8 InPolarity);

	UFUNCTION()
	void OnNPCStunStart(AShooterNPC* StunnedNPC, float Duration);

	UFUNCTION()
	void OnNPCStunEnd(AShooterNPC* StunnedNPC);

	UFUNCTION()
	void OnNPCDamageTaken(AShooterNPC* DamagedNPC, float Damage, TSubclassOf<UDamageType> DamageType, FVector HitLocation, AActor* DamageCauser);
};
