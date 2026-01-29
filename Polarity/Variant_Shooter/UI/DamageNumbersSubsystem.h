// DamageNumbersSubsystem.h
// World subsystem for managing floating damage numbers

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tickable.h"
#include "Variant_Shooter/DamageCategory/PlayerDamageCategory.h"
#include "DamageNumbersSubsystem.generated.h"

class UDamageNumberWidget;
class UCanvasPanel;
class AShooterNPC;

/**
 * Key for identifying unique damage batches (per NPC + per category)
 */
struct FDamageBatchKey
{
	TWeakObjectPtr<AActor> TargetNPC;
	EPlayerDamageCategory Category;

	bool operator==(const FDamageBatchKey& Other) const
	{
		return TargetNPC == Other.TargetNPC && Category == Other.Category;
	}

	friend uint32 GetTypeHash(const FDamageBatchKey& Key)
	{
		return HashCombine(GetTypeHash(Key.TargetNPC), GetTypeHash(static_cast<uint8>(Key.Category)));
	}
};

/**
 * Active damage batch - tracks accumulated damage for a target+category
 */
struct FDamageBatch
{
	float AccumulatedDamage = 0.0f;
	float TimeRemaining = 0.0f;
	FVector WorldLocation = FVector::ZeroVector;
	TObjectPtr<UDamageNumberWidget> ActiveWidget = nullptr;
};

/**
 * Settings for damage number appearance and behavior
 */
USTRUCT(BlueprintType)
struct FDamageNumberSettings
{
	GENERATED_BODY()

	// ==================== Colors by Category ====================

	/** Color for Base damage (melee, ranged) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Colors")
	FLinearColor BaseColor = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);  // White

	/** Color for Kinetic damage (wallslam, momentum, dropkick) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Colors")
	FLinearColor KineticColor = FLinearColor(1.0f, 0.5f, 0.0f, 1.0f);  // Orange

	/** Color for EMF damage (proximity, weapon) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Colors")
	FLinearColor EMFColor = FLinearColor(0.3f, 0.7f, 1.0f, 1.0f);  // Electric Blue

	// ==================== Animation ====================

	/** Vertical offset from hit location (world units) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (ClampMin = "0", ClampMax = "200"))
	float VerticalOffset = 50.0f;

	/** Random horizontal spread (screen pixels) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (ClampMin = "0", ClampMax = "100"))
	float RandomSpreadX = 30.0f;

	/** Random vertical spread (screen pixels) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (ClampMin = "0", ClampMax = "50"))
	float RandomSpreadY = 15.0f;

	// ==================== Scaling ====================

	/** Minimum scale for damage numbers */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scaling", meta = (ClampMin = "0.1", ClampMax = "2.0"))
	float MinScale = 0.8f;

	/** Maximum scale for damage numbers */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scaling", meta = (ClampMin = "0.5", ClampMax = "5.0"))
	float MaxScale = 2.0f;

	/** Damage amount that corresponds to MaxScale */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scaling", meta = (ClampMin = "10", ClampMax = "500"))
	float DamageForMaxScale = 100.0f;

	// ==================== Visibility ====================

	/** Maximum distance to show damage numbers (world units) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visibility", meta = (ClampMin = "1000", ClampMax = "50000"))
	float MaxDistance = 10000.0f;

	/** Minimum damage to show (filters tiny damage ticks) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visibility", meta = (ClampMin = "0", ClampMax = "10"))
	float MinDamageToShow = 1.0f;

	// ==================== Pool ====================

	/** Maximum number of widgets in the pool */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pool", meta = (ClampMin = "5", ClampMax = "50"))
	int32 PoolSize = 20;

	// ==================== Batching ====================

	/** Enable damage batching (TF2-style cumulative damage numbers) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Batching")
	bool bEnableBatching = true;

	/** Time window for batching damage (seconds). Damage within this window is combined. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Batching", meta = (ClampMin = "0.1", ClampMax = "3.0", EditCondition = "bEnableBatching"))
	float BatchingWindow = 0.5f;
};

/**
 * World subsystem that manages floating damage numbers
 * Handles widget pooling and screen position updates
 * Implements FTickableGameObject for per-frame batch timer updates
 */
UCLASS()
class POLARITY_API UDamageNumbersSubsystem : public UWorldSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	// ==================== Subsystem Lifecycle ====================

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	// ==================== FTickableGameObject Interface ====================

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UDamageNumbersSubsystem, STATGROUP_Tickables); }
	virtual bool IsTickable() const override { return !IsTemplate() && bEnabled; }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual UWorld* GetTickableGameObjectWorld() const override { return GetWorld(); }

	// ==================== Main API ====================

	/**
	 * Spawn a floating damage number at a world location
	 * @param WorldLocation The world position where damage occurred
	 * @param Damage The damage amount to display
	 * @param Category The damage category for color coding
	 */
	UFUNCTION(BlueprintCallable, Category = "Damage Numbers")
	void SpawnDamageNumber(const FVector& WorldLocation, float Damage, EPlayerDamageCategory Category);

	/**
	 * Spawn a damage number using damage type class for automatic categorization
	 * @param WorldLocation The world position where damage occurred
	 * @param Damage The damage amount to display
	 * @param DamageTypeClass The damage type class (will be converted to category)
	 */
	UFUNCTION(BlueprintCallable, Category = "Damage Numbers")
	void SpawnDamageNumberFromType(const FVector& WorldLocation, float Damage, TSubclassOf<UDamageType> DamageTypeClass);

	// ==================== Settings ====================

	/** Damage number settings */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	FDamageNumberSettings Settings;

	/** Widget class to use for damage numbers (must inherit from UDamageNumberWidget) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	TSubclassOf<UDamageNumberWidget> WidgetClass;

	/** Enable/disable damage numbers globally */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	bool bEnabled = true;

	// ==================== NPC Registration ====================

	/**
	 * Register an NPC to show damage numbers when it takes damage
	 * Call this from NPC's BeginPlay
	 * @param NPC The NPC to register
	 */
	UFUNCTION(BlueprintCallable, Category = "Damage Numbers")
	void RegisterNPC(AShooterNPC* NPC);

	/**
	 * Unregister an NPC (call on death/destroy)
	 * @param NPC The NPC to unregister
	 */
	UFUNCTION(BlueprintCallable, Category = "Damage Numbers")
	void UnregisterNPC(AShooterNPC* NPC);

	// ==================== Utility ====================

	/**
	 * Get color for a damage category
	 */
	UFUNCTION(BlueprintPure, Category = "Damage Numbers")
	FLinearColor GetColorForCategory(EPlayerDamageCategory Category) const;

	/**
	 * Calculate scale based on damage amount
	 */
	UFUNCTION(BlueprintPure, Category = "Damage Numbers")
	float CalculateScaleForDamage(float Damage) const;

protected:
	// ==================== NPC Damage Handler ====================

	/** Handle damage taken by registered NPC */
	UFUNCTION()
	void OnNPCDamageTaken(AShooterNPC* DamagedNPC, float Damage, TSubclassOf<UDamageType> DamageType, FVector HitLocation, AActor* DamageCauser);

	/** Registered NPCs for damage number display */
	UPROPERTY()
	TArray<TWeakObjectPtr<AShooterNPC>> RegisteredNPCs;

public:
	// ==================== Widget Pool ====================

	/** Pool of available (inactive) widgets */
	UPROPERTY()
	TArray<TObjectPtr<UDamageNumberWidget>> WidgetPool;

	/** Currently active widgets */
	UPROPERTY()
	TArray<TObjectPtr<UDamageNumberWidget>> ActiveWidgets;

	/** Canvas panel to add widgets to (created at runtime) */
	UPROPERTY()
	TObjectPtr<UCanvasPanel> CanvasPanel;

	/** Get a widget from the pool (or create new if pool empty) */
	UDamageNumberWidget* GetWidgetFromPool();

	/** Return a widget to the pool */
	void ReturnWidgetToPool(UDamageNumberWidget* Widget);

	/** Create the widget pool */
	void CreateWidgetPool();

	/** Clean up all widgets */
	void CleanupWidgets();

	// ==================== Helpers ====================

	/** Get the local player controller */
	APlayerController* GetLocalPlayerController() const;

	/** Convert world location to screen position */
	bool WorldToScreen(const FVector& WorldLocation, FVector2D& OutScreenPosition) const;

	/** Check if a world location is visible on screen */
	bool IsLocationVisible(const FVector& WorldLocation) const;

	// ==================== Batching ====================

	/** Active damage batches - keyed by NPC + Category */
	TMap<FDamageBatchKey, FDamageBatch> ActiveBatches;

	/** Process damage with batching logic */
	void ProcessDamageWithBatching(AActor* TargetNPC, float Damage, EPlayerDamageCategory Category, const FVector& WorldLocation);

	/** Finalize a batch (called when timer expires) */
	void FinalizeBatch(const FDamageBatchKey& Key);
};
