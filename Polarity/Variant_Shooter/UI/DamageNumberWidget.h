// DamageNumberWidget.h
// Base class for floating damage number widgets

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Variant_Shooter/DamageCategory/PlayerDamageCategory.h"
#include "DamageNumberWidget.generated.h"

DECLARE_DELEGATE(FOnDamageNumberFinished);

/**
 * Base class for floating damage number widgets
 * Inherit in Blueprint to create the visual representation
 */
UCLASS(Abstract, Blueprintable)
class POLARITY_API UDamageNumberWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	/**
	 * Initialize the widget with damage data
	 * @param InDamage The damage amount to display
	 * @param InCategory The damage category for color coding
	 * @param InWorldLocation The world position where damage occurred
	 */
	UFUNCTION(BlueprintCallable, Category = "Damage Number")
	void Initialize(float InDamage, EPlayerDamageCategory InCategory, const FVector& InWorldLocation);

	/**
	 * Called by Blueprint when animation is complete
	 * Returns the widget to the pool
	 */
	UFUNCTION(BlueprintCallable, Category = "Damage Number")
	void NotifyAnimationFinished();

	/**
	 * Reset the widget for reuse from pool
	 */
	UFUNCTION(BlueprintCallable, Category = "Damage Number")
	void ResetWidget();

	/**
	 * Update damage value (for batching - adds to existing damage)
	 * Resets the float animation to make the number "pop"
	 * @param AdditionalDamage Damage to add to current total
	 */
	UFUNCTION(BlueprintCallable, Category = "Damage Number")
	void UpdateDamage(float AdditionalDamage);

	// ==================== Blueprint Events ====================

	/**
	 * Called when the widget should play its animation
	 * Implement in Blueprint to create the visual effect
	 * @param Damage The damage amount
	 * @param Category The damage category
	 * @param Color The color to use based on category
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Damage Number",
		meta = (DisplayName = "Play Damage Animation"))
	void BP_PlayDamageAnimation(float Damage, EPlayerDamageCategory Category, FLinearColor Color);

	/**
	 * Called when the widget is being reset for pool reuse
	 * Implement in Blueprint to reset any visual state
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Damage Number",
		meta = (DisplayName = "On Widget Reset"))
	void BP_OnWidgetReset();

	/**
	 * Called when damage is added to an existing number (batching)
	 * Implement in Blueprint to update the display and add a "pop" effect
	 * @param NewTotalDamage The new accumulated damage total
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Damage Number",
		meta = (DisplayName = "On Damage Updated"))
	void BP_OnDamageUpdated(float NewTotalDamage);

	// ==================== Getters ====================

	UFUNCTION(BlueprintPure, Category = "Damage Number")
	float GetDamageValue() const { return DamageValue; }

	UFUNCTION(BlueprintPure, Category = "Damage Number")
	EPlayerDamageCategory GetDamageCategory() const { return DamageCategory; }

	UFUNCTION(BlueprintPure, Category = "Damage Number")
	FVector GetWorldLocation() const { return WorldLocation; }

	UFUNCTION(BlueprintPure, Category = "Damage Number")
	bool IsActive() const { return bIsActive; }

	// ==================== Delegate ====================

	/** Called when the widget finishes its animation and should return to pool */
	FOnDamageNumberFinished OnFinished;

protected:
	/** The damage amount being displayed */
	UPROPERTY(BlueprintReadOnly, Category = "Damage Number")
	float DamageValue = 0.0f;

	/** The damage category for color coding */
	UPROPERTY(BlueprintReadOnly, Category = "Damage Number")
	EPlayerDamageCategory DamageCategory = EPlayerDamageCategory::Base;

	/** The world location where damage occurred */
	UPROPERTY(BlueprintReadOnly, Category = "Damage Number")
	FVector WorldLocation = FVector::ZeroVector;

	/** Whether this widget is currently active (displaying damage) */
	UPROPERTY(BlueprintReadOnly, Category = "Damage Number")
	bool bIsActive = false;

	/** Color associated with current damage category (set by subsystem) */
	UPROPERTY(BlueprintReadOnly, Category = "Damage Number")
	FLinearColor CategoryColor = FLinearColor::White;

	/** Current vertical offset for floating animation (world units) */
	UPROPERTY(BlueprintReadOnly, Category = "Damage Number")
	float CurrentVerticalOffset = 0.0f;

	/** Time elapsed since spawn */
	float ElapsedTime = 0.0f;

public:
	/** Set the color for this damage number (called by subsystem) */
	void SetCategoryColor(const FLinearColor& InColor) { CategoryColor = InColor; }

	/** Speed at which numbers float upward (world units per second) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Damage Number|Animation")
	float FloatSpeed = 100.0f;

	/** Widget half-size for centering (pixels) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Damage Number|Layout")
	FVector2D WidgetHalfSize = FVector2D(100.0f, 25.0f);
};
