// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "CheckpointData.generated.h"

/**
 * Stores player state at checkpoint activation.
 * Used for respawning player with saved state.
 */
USTRUCT(BlueprintType)
struct POLARITY_API FCheckpointData
{
	GENERATED_BODY()

	/** Transform where player will respawn */
	UPROPERTY(BlueprintReadWrite, Category = "Checkpoint")
	FTransform SpawnTransform = FTransform::Identity;

	/** Health at checkpoint (will be restored on respawn) */
	UPROPERTY(BlueprintReadWrite, Category = "Checkpoint")
	float Health = 0.0f;

	/** Base EMF charge at checkpoint (bonus charge is reset) */
	UPROPERTY(BlueprintReadWrite, Category = "Checkpoint")
	float BaseEMFCharge = 0.0f;

	/** Index of currently equipped weapon */
	UPROPERTY(BlueprintReadWrite, Category = "Checkpoint")
	int32 CurrentWeaponIndex = 0;

	/** Ammo counts per weapon index */
	UPROPERTY(BlueprintReadWrite, Category = "Checkpoint")
	TMap<int32, int32> WeaponAmmo;

	/** Whether this checkpoint data is valid for use */
	UPROPERTY(BlueprintReadOnly, Category = "Checkpoint")
	bool bIsValid = false;

	/** Unique ID of the checkpoint actor that created this data */
	UPROPERTY(BlueprintReadOnly, Category = "Checkpoint")
	FGuid CheckpointID;

	/**
	 * Optional: Data for skipable sequences (cutscenes, etc.)
	 * Format is game-specific, stored as generic map for future extensibility
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Checkpoint|Sequences")
	TMap<FName, bool> CompletedSequences;

	/** Upgrade tags acquired by the player at this checkpoint */
	UPROPERTY(BlueprintReadWrite, Category = "Checkpoint|Upgrades")
	TArray<FGameplayTag> AcquiredUpgrades;

	/** Invalidate this checkpoint data */
	void Invalidate()
	{
		bIsValid = false;
		SpawnTransform = FTransform::Identity;
		Health = 0.0f;
		BaseEMFCharge = 0.0f;
		CurrentWeaponIndex = 0;
		WeaponAmmo.Empty();
		CheckpointID.Invalidate();
		CompletedSequences.Empty();
		AcquiredUpgrades.Empty();
	}
};
