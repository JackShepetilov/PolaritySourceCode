// VFXVariantSequenceSubsystem.h
// Keeps per-world pseudo-random variant history for configurable Niagara systems.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "VFXVariantSequenceSubsystem.generated.h"

class UNiagaraComponent;
class UNiagaraSystem;

/**
 * Provides shuffle-bag selection for Niagara systems which expose:
 * - User.VariantCount
 * - User.VariantRecentWindow
 * - User.VariantIndex
 *
 * State belongs to the world and is keyed by the Niagara system asset, so callers
 * do not need to own history or know anything about the variants themselves.
 */
UCLASS()
class POLARITY_API UVFXVariantSequenceSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Deinitialize() override;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	/**
	 * If the component's system exposes the variant parameters, chooses a new
	 * index and writes it before activation. Returns false for ordinary systems.
	 */
	UFUNCTION(BlueprintCallable, Category = "VFX|Variants")
	bool ConfigureVariantForComponent(UNiagaraComponent* Component);

private:
	struct FVariantSequenceState
	{
		int32 VariantCount = 0;
		TArray<int32> Bag;
		TArray<int32> Recent;
	};

	static bool TryGetSystemFloatParameter(
		const UNiagaraSystem* System,
		FName ParameterName,
		float& OutValue);

	int32 DrawVariant(
		const UNiagaraSystem* System,
		int32 VariantCount,
		int32 RecentWindow);

	TMap<FName, FVariantSequenceState> SequenceStates;
};
