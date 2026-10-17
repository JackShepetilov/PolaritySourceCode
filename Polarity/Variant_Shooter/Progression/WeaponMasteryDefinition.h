// WeaponMasteryDefinition.h
// Mastery of a weapon: the levels a player climbs by fighting with it, and what each level gives his
// hands [author, 2026-10-16]. Docs/GameDesign_Tactical_To_PowerFantasy_2026-10-11.md section 11,
// Docs/Research_Weapon_Progression_CoD_BF_2026-10-16.md.
//
// A weapon points at one of these (AShooterWeapon::MasteryDefinition). Progress is kept per asset,
// not per gun: two rifles given the same asset share their mastery, the way a class would; a gun with
// an asset of its own has a mastery of its own.
//
// Mastery only ever helps: its time multipliers are 1 or less, the attachments' are 1 or more, and
// both are read in the one place (AShooterWeapon::GetAttachmentHandlingMultiplier).

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "WeaponMasteryDefinition.generated.h"

/** Time multipliers a level gives. 0: as at the level before. 0.8 makes that time 20% shorter. */
USTRUCT(BlueprintType)
struct FWeaponMasteryHandling
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mastery|ADS", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ADSBlendInTimeMultiplier = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mastery|ADS", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ADSBlendOutTimeMultiplier = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mastery|Switch", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DrawDurationMultiplier = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mastery|Switch", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HolsterDurationMultiplier = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mastery|Sprint", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SprintToFireTimeMultiplier = 0.0f;

	/** The reload of an empty magazine. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mastery|Reload", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ReloadDurationMultiplier = 0.0f;

	/** The reload with rounds left. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mastery|Reload", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SecondaryReloadTimeMultiplier = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mastery|Reload|Per Round", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ReloadStartDurationMultiplier = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mastery|Reload|Per Round", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ReloadLoopDurationMultiplier = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mastery|Reload|Per Round", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ReloadEndDurationMultiplier = 0.0f;

	/** How fast the hands tire in the sight. 0.5: half as fast. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mastery|ADS Fatigue", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ADSFatigueRateMultiplier = 0.0f;

	/** How far a tired sight wanders. 0.5: half as far. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mastery|ADS Fatigue", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ADSFatigueSwayMultiplier = 0.0f;
};

/** One level. Steps[0] is level 1. What a step opens stays open on the levels after it. */
USTRUCT(BlueprintType)
struct FWeaponMasteryStep
{
	GENERATED_BODY()

	/** Experience in all, with this mastery, that reaches the level. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mastery", meta = (ClampMin = "1"))
	int32 XPToReach = 10;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mastery")
	FWeaponMasteryHandling Handling;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mastery|Opens")
	bool bAllowADSWhileSliding = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mastery|Opens")
	bool bAllowADSWhileAirborne = false;

	/** Without it a reload takes the gun out of the sight until it is done. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mastery|Opens")
	bool bAllowReloadWhileADS = false;

	/** Without it a reload cannot be started in a slide. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mastery|Opens")
	bool bAllowReloadWhileSliding = false;

	/** Walking in the sight at full speed (MovementSettings::ADSSpeed no longer caps it). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mastery|Opens")
	bool bIgnoreADSMoveSpeedPenalty = false;

	/** Sprint pressed twice: the tactical sprint (MovementSettings, Tactical Sprint). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mastery|Opens")
	bool bAllowTacticalSprint = false;

	/** A kill makes switching to this gun faster for a while. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mastery|Opens")
	bool bFastSwapAfterKill = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mastery|Opens", meta = (EditCondition = "bFastSwapAfterKill", ClampMin = "0.0", Units = "s"))
	float FastSwapWindow = 3.0f;

	/** Holster and draw play this many times faster inside the window. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mastery|Opens", meta = (EditCondition = "bFastSwapAfterKill", ClampMin = "1.0"))
	float FastSwapSpeedMultiplier = 3.0f;

	/** Throwing the gun like a spear. A stub: the throw itself is not made yet [author, 2026-10-16]. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mastery|Opens")
	bool bCanThrowWeapon = false;
};

/** What a mastery gives at one level, every step up to it put together. */
USTRUCT(BlueprintType)
struct FWeaponMasteryPerks
{
	GENERATED_BODY()

	/** 1 where no step has said. */
	UPROPERTY(BlueprintReadOnly, Category = "Mastery")
	FWeaponMasteryHandling Handling;

	UPROPERTY(BlueprintReadOnly, Category = "Mastery") bool bAllowADSWhileSliding = false;
	UPROPERTY(BlueprintReadOnly, Category = "Mastery") bool bAllowADSWhileAirborne = false;
	UPROPERTY(BlueprintReadOnly, Category = "Mastery") bool bAllowReloadWhileADS = false;
	UPROPERTY(BlueprintReadOnly, Category = "Mastery") bool bAllowReloadWhileSliding = false;
	UPROPERTY(BlueprintReadOnly, Category = "Mastery") bool bIgnoreADSMoveSpeedPenalty = false;
	UPROPERTY(BlueprintReadOnly, Category = "Mastery") bool bAllowTacticalSprint = false;
	UPROPERTY(BlueprintReadOnly, Category = "Mastery") bool bFastSwapAfterKill = false;
	UPROPERTY(BlueprintReadOnly, Category = "Mastery") float FastSwapWindow = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Mastery") float FastSwapSpeedMultiplier = 1.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Mastery") bool bCanThrowWeapon = false;

	FWeaponMasteryPerks();
};

UCLASS(BlueprintType)
class POLARITY_API UWeaponMasteryDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:

	/** For the HUD and the log. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mastery")
	FText DisplayName;

	/** Experience for every hit with this gun on an enemy. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mastery", meta = (ClampMin = "0"))
	int32 XPPerHit = 1;

	/** Experience on top of the hit when that hit kills. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mastery", meta = (ClampMin = "0"))
	int32 XPPerKill = 5;

	/** The ladder, level 1 first. XPToReach has to grow from step to step. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mastery")
	TArray<FWeaponMasteryStep> Steps;

	int32 GetLevelForXP(int32 XP) const;

	/** Experience in all for NextLevel; -1 above the top. */
	int32 GetXPToReach(int32 NextLevel) const;

	/** Everything the steps up to Level give. */
	FWeaponMasteryPerks Resolve(int32 Level) const;
};
