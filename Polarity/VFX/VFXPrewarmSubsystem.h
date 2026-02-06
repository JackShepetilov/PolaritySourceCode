// VFXPrewarmSubsystem.h
// Prewarms Niagara systems at level start to avoid runtime shader compilation hitches

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Engine/DataAsset.h"
#include "VFXPrewarmSubsystem.generated.h"

class UNiagaraSystem;
class UNiagaraComponent;

/**
 * Data asset containing list of Niagara systems to prewarm.
 * Create one in Content Browser: Right-click → Miscellaneous → Data Asset → VFXPrewarmList
 */
UCLASS(BlueprintType)
class POLARITY_API UVFXPrewarmList : public UDataAsset
{
	GENERATED_BODY()

public:

	/** List of Niagara systems to prewarm at level start */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX Prewarm")
	TArray<TSoftObjectPtr<UNiagaraSystem>> SystemsToPrewarm;
};

/**
 * World subsystem that prewarms Niagara VFX systems at level load.
 * Spawns each system once off-screen to trigger shader compilation,
 * preventing hitches when effects are first used during gameplay.
 *
 * Usage:
 * 1. Create a UVFXPrewarmList Data Asset in Content Browser
 * 2. Add all your Niagara systems to it
 * 3. Set PrewarmListPath in Project Settings or call SetPrewarmList()
 * 4. Call PrewarmAllSystems() at level start (e.g., from GameMode or Level Blueprint)
 */
UCLASS()
class POLARITY_API UVFXPrewarmSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:

	//~ Begin USubsystem Interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	//~ End USubsystem Interface

	/**
	 * Set the prewarm list to use. Call before PrewarmAllSystems().
	 */
	UFUNCTION(BlueprintCallable, Category = "VFX|Prewarm")
	void SetPrewarmList(UVFXPrewarmList* InPrewarmList);

	/**
	 * Register a single Niagara system to be prewarmed.
	 * Can be called from BeginPlay of actors that use VFX.
	 */
	UFUNCTION(BlueprintCallable, Category = "VFX|Prewarm")
	void RegisterSystemForPrewarm(UNiagaraSystem* System);

	/**
	 * Register multiple systems at once
	 */
	UFUNCTION(BlueprintCallable, Category = "VFX|Prewarm")
	void RegisterSystemsForPrewarm(const TArray<UNiagaraSystem*>& Systems);

	/**
	 * Trigger prewarm of all registered systems.
	 * Call this at level start, ideally during loading screen.
	 */
	UFUNCTION(BlueprintCallable, Category = "VFX|Prewarm")
	void PrewarmAllSystems();

	/**
	 * Check if prewarm has completed
	 */
	UFUNCTION(BlueprintPure, Category = "VFX|Prewarm")
	bool IsPrewarmComplete() const { return bPrewarmComplete; }

	/**
	 * Get number of systems that were prewarmed
	 */
	UFUNCTION(BlueprintPure, Category = "VFX|Prewarm")
	int32 GetPrewarmedSystemCount() const { return PrewarmedCount; }

protected:

	/** Load and register systems from prewarm list */
	void LoadPrewarmList();

	/** Spawn a single system for prewarming */
	void PrewarmSystem(UNiagaraSystem* System);

	/** Called after prewarm delay to clean up */
	void OnPrewarmComplete();

	/** Timer handle for prewarm completion */
	FTimerHandle PrewarmTimerHandle;

	/** Prewarm list data asset */
	UPROPERTY()
	TObjectPtr<UVFXPrewarmList> PrewarmList;

	/** Systems registered for prewarming */
	UPROPERTY()
	TSet<TObjectPtr<UNiagaraSystem>> SystemsToPrewarm;

	/** Spawned prewarm components (destroyed after prewarm) */
	UPROPERTY()
	TArray<TObjectPtr<UNiagaraComponent>> PrewarmComponents;

	/** Location far below the level for prewarming (invisible to player) */
	FVector PrewarmLocation = FVector(0.0f, 0.0f, -100000.0f);

	/** Duration to keep prewarm effects alive before cleanup */
	float PrewarmDuration = 0.5f;

	/** True after prewarm sequence completes */
	bool bPrewarmComplete = false;

	/** True if prewarm has started */
	bool bPrewarmStarted = false;

	/** Number of systems that were prewarmed */
	int32 PrewarmedCount = 0;
};
