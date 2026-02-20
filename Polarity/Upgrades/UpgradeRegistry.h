// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "UpgradeRegistry.generated.h"

class UUpgradeDefinition;

/**
 * Central catalog of all available upgrades in the game.
 * Used by UpgradeManagerComponent to resolve GameplayTags back to definitions on load.
 * Create ONE of these in the Content Browser and populate with all upgrade definitions.
 */
UCLASS(BlueprintType)
class POLARITY_API UUpgradeRegistry : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:

	/** All available upgrades in the game */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Upgrades")
	TArray<TObjectPtr<UUpgradeDefinition>> AllUpgrades;

	/** Find an upgrade definition by its gameplay tag */
	UFUNCTION(BlueprintPure, Category = "Upgrades")
	const UUpgradeDefinition* FindByTag(FGameplayTag Tag) const;

	// UPrimaryDataAsset interface
	virtual FPrimaryAssetId GetPrimaryAssetId() const override
	{
		return FPrimaryAssetId(TEXT("UpgradeRegistry"), GetFName());
	}
};
