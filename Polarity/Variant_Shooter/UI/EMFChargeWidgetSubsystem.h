// EMFChargeWidgetSubsystem.h
// World subsystem for managing EMF charge indicator widgets above NPCs and Props

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tickable.h"
#include "EMFChargeWidgetSubsystem.generated.h"

class UEMFChargeWidget;
class AShooterNPC;
class AEMFPhysicsProp;

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

	/** Maximum widgets in pool */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pool", meta = (ClampMin = "5", ClampMax = "50"))
	int32 PoolSize = 20;
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
	void CreateWidgetPool();
	void CleanupWidgets();
	void ProcessPendingRegistrations();

	APlayerController* GetLocalPlayerController() const;

	/** Actors that tried to register before WidgetClass was set */
	TArray<TWeakObjectPtr<AShooterNPC>> PendingNPCs;
	TArray<TWeakObjectPtr<AEMFPhysicsProp>> PendingProps;
};
