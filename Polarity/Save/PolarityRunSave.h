// PolarityRunSave.h
// Volatile mid-run resume save object. Slot "Polarity_Run".
// Created on StartRun, overwritten per-arena, DELETED on death/victory, KEPT on quit-to-menu
// (its presence on disk is the "you have a run to resume" signal).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "GameplayTagContainer.h"
#include "PolaritySaveTypes.h"
#include "PolarityRunSave.generated.h"

UCLASS()
class POLARITY_API UPolarityRunSave : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY()
	int32 SaveVersion = PolaritySaveVersion::RunInitial;

	// ---- URunSubsystem run-tier ----
	// NOTE: the uint8 is the persisted contract; the C++ ERunState enum is NOT. Append enumerators
	// only, never reorder None/Active/Paused/Ended.
	UPROPERTY()
	uint8 RunState = 0;

	UPROPERTY()
	int32 CurrentArenaIndex = -1;

	UPROPERTY()
	int32 ActivatedAntennaCount = 0;

	/** Run-scoped upgrade ledger (tag -> level). Tag keys are rename-tolerant. */
	UPROPERTY()
	TMap<FGameplayTag, int32> AcquiredUpgrades;

	// ---- FRunStats flattened ----
	UPROPERTY()
	int32 TotalXPEarned = 0;

	UPROPERTY()
	int32 LevelsGained = 0;

	UPROPERTY()
	float RunDuration = 0.f;

	/** FRunStats::KillsByEnemy is TMap<TSubclassOf<AShooterNPC>,int32>. TSubclassOf keys do NOT
	 *  round-trip cleanly, so we flatten to soft-class-path string keys and resolve on load. */
	UPROPERTY()
	TMap<FString, int32> KillsByEnemyClassPath;

	// ---- UXPSubsystem::Progress (FSkillState) ----
	UPROPERTY()
	int32 XP_CurrentXP = 0;

	UPROPERTY()
	int32 XP_CurrentLevel = 0;
};
