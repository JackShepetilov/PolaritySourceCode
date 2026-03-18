// EMFChargeWidgetSubsystem.h
// World subsystem for managing EMF charge indicator widgets above NPCs and Props

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tickable.h"
#include "EMFChargeWidget.h"
#include "EMFChargeWidgetSubsystem.generated.h"

class UEMFChargeWidget;
class AShooterNPC;
class AEMFPhysicsProp;
class ADroppedMeleeWeapon;
class ADroppedRangedWeapon;

/**
 * Per-category settings for widget clutter reduction.
 * Controls how the widget's own MinScaleDistance is multiplied down as more actors
 * of this category are registered. Base distance comes from the widget blueprint.
 * Formula: Multiplier = MinMultiplier + (1 - MinMultiplier) / (1 + DecayRate * (Count - 1))
 */
USTRUCT(BlueprintType)
struct FWidgetClutterSettings
{
	GENERATED_BODY()

	/** Base cutoff distance for this category (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clutter", meta = (ClampMin = "500", ClampMax = "20000"))
	float BaseMinScaleDistance = 3000.0f;

	/** How fast distance shrinks with count (higher = more aggressive reduction) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clutter", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float DecayRate = 0.15f;

	/** Floor multiplier — never reduce below this fraction of BaseMinScaleDistance */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clutter", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float MinMultiplier = 0.3f;

	/** Compute effective MinScaleDistance for a given count */
	float ComputeEffectiveDistance(int32 Count) const
	{
		if (Count <= 1)
		{
			return BaseMinScaleDistance;
		}
		float Mult = MinMultiplier + (1.0f - MinMultiplier) / (1.0f + DecayRate * (Count - 1));
		return BaseMinScaleDistance * Mult;
	}
};

/**
 * Settings for EMF charge widget appearance
 */
USTRUCT(BlueprintType)
struct FEMFChargeWidgetSettings
{
	GENERATED_BODY()

	/** Maximum distance to show charge widgets (world units) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visibility", meta = (ClampMin = "1000", ClampMax = "50000"))
	float MaxDistance = 10000.0f;

	/** Vertical offset above NPC capsule top (world units) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ClampMin = "0", ClampMax = "200"))
	float NPCVerticalOffset = 30.0f;

	/** Vertical offset above prop bounds top (world units) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ClampMin = "0", ClampMax = "200"))
	float PropVerticalOffset = 30.0f;

	/** Clutter reduction settings for NPC widgets */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clutter")
	FWidgetClutterSettings NPCClutter;

	/** Clutter reduction settings for EMF Prop widgets */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clutter")
	FWidgetClutterSettings PropClutter;

	/** Clutter reduction settings for Dropped Weapon widgets */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clutter")
	FWidgetClutterSettings WeaponClutter;
};

/**
 * World subsystem that manages overhead EMF charge indicator widgets.
 * Supports both ShooterNPC and EMFPhysicsProp targets.
 * Implements FTickableGameObject to update widget positions independently of Slate paint.
 */
UCLASS()
class POLARITY_API UEMFChargeWidgetSubsystem : public UWorldSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	// ==================== Subsystem Lifecycle ====================

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	// ==================== FTickableGameObject Interface ====================

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UEMFChargeWidgetSubsystem, STATGROUP_Tickables); }
	virtual bool IsTickable() const override { return !IsTemplate() && bEnabled; }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual UWorld* GetTickableGameObjectWorld() const override { return GetWorld(); }

	// ==================== NPC API ====================

	UFUNCTION(BlueprintCallable, Category = "EMF Charge Widget")
	void RegisterNPC(AShooterNPC* NPC);

	UFUNCTION(BlueprintCallable, Category = "EMF Charge Widget")
	void UnregisterNPC(AShooterNPC* NPC);

	// ==================== Prop API ====================

	UFUNCTION(BlueprintCallable, Category = "EMF Charge Widget")
	void RegisterProp(AEMFPhysicsProp* Prop);

	UFUNCTION(BlueprintCallable, Category = "EMF Charge Widget")
	void UnregisterProp(AEMFPhysicsProp* Prop);

	// ==================== Dropped Weapon API ====================

	UFUNCTION(BlueprintCallable, Category = "EMF Charge Widget")
	void RegisterDroppedWeapon(ADroppedMeleeWeapon* Weapon);

	UFUNCTION(BlueprintCallable, Category = "EMF Charge Widget")
	void UnregisterDroppedWeapon(ADroppedMeleeWeapon* Weapon);

	// ==================== Dropped Ranged Weapon API ====================

	UFUNCTION(BlueprintCallable, Category = "EMF Charge Widget")
	void RegisterDroppedRangedWeapon(ADroppedRangedWeapon* Weapon);

	UFUNCTION(BlueprintCallable, Category = "EMF Charge Widget")
	void UnregisterDroppedRangedWeapon(ADroppedRangedWeapon* Weapon);

	// ==================== Settings ====================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	FEMFChargeWidgetSettings Settings;

	/** Widget class to use (must inherit from UEMFChargeWidget) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	TSubclassOf<UEMFChargeWidget> WidgetClass;

	/** Enable/disable the entire system */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	bool bEnabled = true;

protected:
	UFUNCTION()
	void OnNPCDied(AShooterNPC* DeadNPC);

	UFUNCTION()
	void OnPropDied(AEMFPhysicsProp* Prop, AActor* Killer);

	// ==================== Widget Pool ====================

	UPROPERTY()
	TArray<TObjectPtr<UEMFChargeWidget>> WidgetPool;

	/** All active widgets (keyed by target actor) */
	UPROPERTY()
	TMap<TWeakObjectPtr<AActor>, TObjectPtr<UEMFChargeWidget>> ActiveWidgets;

	UEMFChargeWidget* GetWidgetFromPool();
	void ReturnWidgetToPool(UEMFChargeWidget* Widget);
	void CleanupWidgets();
	void ProcessPendingRegistrations();

	APlayerController* GetLocalPlayerController() const;

	/** Get clutter settings for a given category */
	const FWidgetClutterSettings& GetClutterSettings(EChargeWidgetCategory Category) const;

	/** Actors that tried to register before WidgetClass was set */
	TArray<TWeakObjectPtr<AShooterNPC>> PendingNPCs;
	TArray<TWeakObjectPtr<AEMFPhysicsProp>> PendingProps;

};
