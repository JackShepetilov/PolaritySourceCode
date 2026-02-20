// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "UpgradeDefinition.generated.h"

class UUpgradeComponent;

/**
 * Data asset that defines an upgrade's metadata and logic class.
 * Created in the editor Content Browser. One per upgrade.
 */
UCLASS(BlueprintType)
class POLARITY_API UUpgradeDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:

	/** Unique gameplay tag identifier for this upgrade (used for save/load and dedup) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Upgrade")
	FGameplayTag UpgradeTag;

	/** Display name shown in UI and on world pickup */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Upgrade|UI")
	FText DisplayName;

	/** Description shown in UI */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Upgrade|UI")
	FText Description;

	/** Icon for UI and world pickup hologram */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Upgrade|UI")
	TObjectPtr<UTexture2D> Icon;

	/** The component class that implements this upgrade's runtime logic */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Upgrade")
	TSubclassOf<UUpgradeComponent> ComponentClass;

	/** Visual tier for pickup VFX treatment (color, particles, etc.) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Upgrade|UI", meta = (ClampMin = "1", ClampMax = "5"))
	int32 Tier = 1;

	// UPrimaryDataAsset interface
	virtual FPrimaryAssetId GetPrimaryAssetId() const override
	{
		return FPrimaryAssetId(TEXT("UpgradeDefinition"), GetFName());
	}
};
