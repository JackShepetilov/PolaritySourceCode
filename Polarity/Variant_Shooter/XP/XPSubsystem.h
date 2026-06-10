// XPSubsystem.h
// GameInstance subsystem owning the single level track for a roguelite run.
//
// Levels come from arena antennas: ArenaManager::HandleAntennaActivated calls GrantLevel()
// once per uploaded antenna. Kills do NOT award XP — they only feed run kill stats.
// The legacy XP pool (AddXP + XPConfig thresholds) remains for debug commands and possible
// future non-antenna sources. On level-up the subsystem broadcasts OnLevelUp(NewLevel);
// the upgrade-choice UI then rolls random upgrades from the whole registry.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "RunSubsystem.h"
#include "SkillTypes.h"
#include "XPSubsystem.generated.h"

class UXPConfig;
class AShooterNPC;
class AActor;
class UDamageType;
class UWorld;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnXPGained, int32, Amount, int32, NewTotalXP);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLevelUp, int32, NewLevel);

UCLASS()
class POLARITY_API UXPSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// ==================== Subsystem Lifecycle ====================

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ==================== Public API ====================

	UFUNCTION(BlueprintCallable, Category = "XP")
	void SetConfig(UXPConfig* InConfig);

	/** Award flat XP into the single pool. Legacy/debug path — normal play levels up via GrantLevel. */
	UFUNCTION(BlueprintCallable, Category = "XP")
	void AddXP(int32 Amount);

	/** Grant exactly one level — the antenna-activation reward. Ignores XP thresholds:
	 *  bumps the level, snaps CurrentXP up to the new level's threshold so HUD readouts
	 *  stay consistent, and broadcasts OnLevelUp. No-op if the run is inactive or the
	 *  level cap (XPConfig::GetMaxLevel) is already reached. */
	UFUNCTION(BlueprintCallable, Category = "XP")
	void GrantLevel();

	/** Silently restore the XP track from a save (mid-run resume). Does NOT run level-up logic;
	 *  broadcasts OnXPGained(0, XP) once so HUD bars refresh. */
	void RestoreProgress(int32 InXP, int32 InLevel);

	// ==================== Read API ====================

	UFUNCTION(BlueprintPure, Category = "XP")
	int32 GetCurrentXP() const;

	UFUNCTION(BlueprintPure, Category = "XP")
	int32 GetCurrentLevel() const;

	UFUNCTION(BlueprintPure, Category = "XP")
	int32 GetXPToNextLevel() const;

	UFUNCTION(BlueprintPure, Category = "XP")
	int32 GetXPIntoCurrentLevel() const;

	UFUNCTION(BlueprintPure, Category = "XP")
	int32 GetCurrentLevelSpan() const;

	UFUNCTION(BlueprintPure, Category = "XP")
	float GetProgressToNextLevel01() const;

	// ==================== Events ====================

	UPROPERTY(BlueprintAssignable, Category = "XP|Events")
	FOnXPGained OnXPGained;

	UPROPERTY(BlueprintAssignable, Category = "XP|Events")
	FOnLevelUp OnLevelUp;

	/** Subscribe to an NPC's OnNPCDeathDetailed so its kill routes through HandleNPCDeath.
	 *  Normally called automatically by OnAnyActorSpawned, but recycled NPCs (pool re-use)
	 *  don't fire SpawnActor — call this manually in ResetForPool to restore XP tracking. */
	void BindToNPC(AShooterNPC* NPC);

protected:
	// ==================== Run lifecycle handlers ====================

	UFUNCTION() void HandleRunStarted();
	UFUNCTION() void HandleRunEnded(ERunEndReason Reason);
	UFUNCTION() void HandleArenaEntered(int32 ArenaIndex);

	// ==================== NPC tracking ====================

	void BindNPCEventsForCurrentWorld();
	void UnbindNPCEventsForCurrentWorld();
	void OnAnyActorSpawned(AActor* Actor);

	UFUNCTION()
	void HandleNPCDeath(AShooterNPC* DeadNPC, TSubclassOf<UDamageType> KillingDamageType, AActor* KillingDamageCauser);

	// ==================== Internals ====================

	void CheckLevelUp();
	bool WasKillCausedByPlayer(AActor* DamageCauser) const;
	URunSubsystem* GetRunSubsystem() const;

	// ==================== State ====================

	/** Single XP + level track for the current run. Reset on OnRunStarted. */
	UPROPERTY(SaveGame)
	FSkillState Progress;

	UPROPERTY(Transient)
	TWeakObjectPtr<UXPConfig> Config;

	FDelegateHandle ActorSpawnedHandle;
	TWeakObjectPtr<UWorld> BoundWorld;
	TArray<TWeakObjectPtr<AShooterNPC>> BoundNPCs;
};
