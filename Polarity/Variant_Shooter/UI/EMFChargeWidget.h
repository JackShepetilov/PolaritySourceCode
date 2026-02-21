// EMFChargeWidget.h
// Widget that displays EMF charge above an actor's head (NPC or Physics Prop)

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "EMFChargeWidget.generated.h"

class AShooterNPC;
class AEMFPhysicsProp;

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

protected:
	/** The NPC this widget is tracking (mutually exclusive with BoundProp) */
	TWeakObjectPtr<AShooterNPC> BoundNPC;

	/** The prop this widget is tracking (mutually exclusive with BoundNPC) */
	TWeakObjectPtr<AEMFPhysicsProp> BoundProp;

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
};
