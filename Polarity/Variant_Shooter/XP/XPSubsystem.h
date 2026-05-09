// XPSubsystem.h
// GameInstance subsystem owning per-skill XP and level state for a roguelite run.
//
// State is now keyed by ESkillCategory. Public API takes a Category parameter throughout.
// Kill XP is routed via XPConfig.KillXPRouting (DamageType -> Category) and scaled by
// XPConfig.EnemyXPMultiplier (class -> float, default 1.0).
//
// Trackers (Movement / Melee / EMF / Weapon) for non-kill events are added in Stage Б;
// they will call AddSkillXP(Category, Amount) directly.

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

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnSkillXPGained, ESkillCategory, Category, int32, Amount, int32, NewTotalXP);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSkillLevelUp, ESkillCategory, Category, int32, NewLevel);

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

	/** Award flat XP to a specific skill (no enemy multiplier). Called by trackers in Stage Б. */
	UFUNCTION(BlueprintCallable, Category = "XP")
	void AddSkillXP(ESkillCategory Category, int32 Amount);

	/** Award kill XP for a skill: BaseXPPerKill[Cat] * EnemyMultiplier[EnemyClass]. C++-only (no BP exec wrapper to avoid pulling ShooterNPC.h into this header). */
	void AwardKillXP(ESkillCategory Category, TSubclassOf<AShooterNPC> EnemyClass);

	// ==================== Read API ====================

	UFUNCTION(BlueprintPure, Category = "XP")
	int32 GetCurrentXP(ESkillCategory Category) const;

	UFUNCTION(BlueprintPure, Category = "XP")
	int32 GetCurrentLevel(ESkillCategory Category) const;

	UFUNCTION(BlueprintPure, Category = "XP")
	int32 GetXPToNextLevel(ESkillCategory Category) const;

	UFUNCTION(BlueprintPure, Category = "XP")
	int32 GetXPIntoCurrentLevel(ESkillCategory Category) const;

	UFUNCTION(BlueprintPure, Category = "XP")
	int32 GetCurrentLevelSpan(ESkillCategory Category) const;

	UFUNCTION(BlueprintPure, Category = "XP")
	float GetProgressToNextLevel01(ESkillCategory Category) const;

	// ==================== Events ====================

	UPROPERTY(BlueprintAssignable, Category = "XP|Events")
	FOnSkillXPGained OnSkillXPGained;

	UPROPERTY(BlueprintAssignable, Category = "XP|Events")
	FOnSkillLevelUp OnSkillLevelUp;

protected:
	// ==================== Run lifecycle handlers ====================

	UFUNCTION() void HandleRunStarted();
	UFUNCTION() void HandleRunEnded(ERunEndReason Reason);
	UFUNCTION() void HandleArenaEntered(int32 ArenaIndex);

	// ==================== NPC tracking ====================

	void BindNPCEventsForCurrentWorld();
	void UnbindNPCEventsForCurrentWorld();
	void BindToNPC(AShooterNPC* NPC);
	void OnAnyActorSpawned(AActor* Actor);

	UFUNCTION()
	void HandleNPCDeath(AShooterNPC* DeadNPC, TSubclassOf<UDamageType> KillingDamageType, AActor* KillingDamageCauser);

	// ==================== Internals ====================

	void CheckLevelUpForCategory(ESkillCategory Category);
	bool WasKillCausedByPlayer(AActor* DamageCauser) const;
	URunSubsystem* GetRunSubsystem() const;
	FSkillState& GetOrCreateState(ESkillCategory Category);

	// ==================== State ====================

	UPROPERTY(SaveGame)
	TMap<ESkillCategory, FSkillState> Skills;

	UPROPERTY(Transient)
	TWeakObjectPtr<UXPConfig> Config;

	FDelegateHandle ActorSpawnedHandle;
	TWeakObjectPtr<UWorld> BoundWorld;
	TArray<TWeakObjectPtr<AShooterNPC>> BoundNPCs;
};
