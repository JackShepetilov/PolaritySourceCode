// Runtime assembly of arena streaming levels at persistent biome anchors.
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BiomeRunAssembler.generated.h"

class ABiomeArenaAnchor;
class UBiomeRunRegistry;
class ULevelStreamingDynamic;

/** Unique weak owner for one global Landscape Grass exclusion box. */
UCLASS(Transient)
class UBiomeGrassExclusionHandle : public UObject
{
	GENERATED_BODY()
};

UCLASS()
class POLARITY_API ABiomeRunAssembler : public AActor
{
	GENERATED_BODY()
public:
	ABiomeRunAssembler();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(BlueprintPure, Category = "Biome Run")
	bool IsAssemblyReady() const { return bAssemblyComplete && bAssemblySucceeded; }
	UFUNCTION(BlueprintPure, Category = "Biome Run")
	bool HasAssemblyFailed() const { return bAssemblyComplete && !bAssemblySucceeded; }
	UFUNCTION(BlueprintPure, Category = "Biome Run")
	int32 GetResolvedSeed() const { return ResolvedSeed; }
	UFUNCTION(BlueprintPure, Category = "Biome Run")
	const TArray<FName>& GetSelectedArenaIds() const { return SelectedArenaIds; }

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Biome Run")
	TObjectPtr<UBiomeRunRegistry> Registry;
	/** Non-zero forces a seed for editor/debug runs. Shipping runs leave this at zero. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Biome Run")
	int32 SeedOverride = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Biome Run|Navigation")
	bool bRebuildNavigation = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Biome Run|Navigation", meta = (ClampMin = "1.0"))
	float NavigationWaitTimeout = 30.f;

private:
	void Assemble();
	void PollArenaStreaming();
	void PollNavigation();
	void RegisterGrassExclusions();
	void BindBridgeProgression();
	void FinishAssembly(bool bSuccess, const FString& Reason = FString());
	FName ResolveBiomeId() const;
	FName ResolveLayoutId() const;

	UPROPERTY(Transient)
	TArray<TObjectPtr<ULevelStreamingDynamic>> SpawnedArenaLevels;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UBiomeGrassExclusionHandle>> GrassExclusionHandles;
	UPROPERTY(Transient)
	TArray<TObjectPtr<ABiomeArenaAnchor>> OrderedAnchors;
	TArray<FName> SelectedArenaIds;
	TArray<int32> SelectedOptionIndices;
	FTimerHandle AssemblyPollTimer;
	int32 ResolvedSeed = 0;
	double NavigationWaitStartedAt = 0.0;
	bool bAssemblyComplete = false;
	bool bAssemblySucceeded = false;
};
