// AbilityDefinition_TankPassive.h
// The Tank's always-on ability: three small effects that all pay him for standing in the fire.

#pragma once

#include "CoreMinimal.h"
#include "AbilityDefinition.h"
#include "AbilityDefinition_TankPassive.generated.h"

class AHealthPickup;
class UNiagaraSystem;

USTRUCT(BlueprintType)
struct FTankPassiveLevelStats
{
	GENERATED_BODY()

	// ---- Health from kills nearby ----

	/** How close an enemy has to die for the Tank to get paid for it. Measured from the Tank, not
	 *  from whoever landed the shot: the design says "killed near him", and being the one standing
	 *  where the dying happens IS the Tank's job. A teammate's kill counts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kill Drops", meta = (ClampMin = "0.0", Units = "cm"))
	float KillDropRadius = 1200.0f;

	/** Extra pickups dropped by such a kill, on top of whatever the enemy would have dropped anyway.
	 *  This never suppresses a normal drop; the two simply add. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kill Drops", meta = (ClampMin = "0", ClampMax = "10"))
	int32 KillDropCount = 1;

	// ---- Damage returned ----

	/** Fraction of an incoming hit sent back to whoever landed it, when that enemy's shield is fully
	 *  stripped. Scales down to nothing on an untouched enemy.
	 *
	 *  "Stripped" is read off the enemy itself and needs no number here: shield is the inverse of
	 *  charge on its UEMFVelocityModifier, so zero charge is a whole shield and MaxBaseCharge is no
	 *  shield left -- the same reading the Tank's active uses when it hands shield back, and the same
	 *  instant IsAtMaxCharge() calls the enemy grabbable. The scale is |charge| / MaxBaseCharge, which
	 *  means retuning an enemy's charge cap retunes this with it instead of silently pinning it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reflect", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float ReflectFractionAtFullStrip = 0.5f;

	/** Never send back more than this in one hit, whatever the arithmetic says. A ceiling exists
	 *  because the input is somebody else's damage number, which this ability does not control. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reflect", meta = (ClampMin = "0.0"))
	float MaxReflectPerHit = 40.0f;

	/** Beam drawn from the wound to the enemy that made it, on every machine. Takes the far end as a
	 *  Vec3 user parameter named BeamEndPoint, which is the same contract the channelling capture and
	 *  launch beams use -- one of those assets can be dropped straight in here.
	 *
	 *  Where it lands on the enemy: the same bone, when the enemy's skeleton has one by that name, so
	 *  a shot that hit the Tank in the head comes back at a head. A drone or anything else on another
	 *  skeleton has no such bone and takes it in the centre instead. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reflect")
	TObjectPtr<UNiagaraSystem> ReflectVFX;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reflect")
	FVector ReflectVFXScale = FVector(1.0f, 1.0f, 1.0f);

	/** Played at the enemy's end of the beam, on every machine, the moment the reflect fires. Not
	 *  attached to anything: the return is instantaneous, so there is nothing for the sound to follow
	 *  for longer than the one shot it announces. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reflect")
	TObjectPtr<USoundBase> ReflectSound;

	// ---- Provocation ----

	/** Threat added per shot fired. Threat is not a running total: it is a pile of impulses that each
	 *  fade, so sustained fire holds attention and a single shot does not. 1.0 makes the Tank look
	 *  about half as far away as he is. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Provoke", meta = (ClampMin = "0.0"))
	float ThreatPerShot = 0.5f;

	/** How long one shot's worth of provocation takes to fade. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Provoke", meta = (ClampMin = "0.1", Units = "s"))
	float ThreatDecaySeconds = 4.0f;
};

/**
 * The Tank's passive, from the design document: enemies killed near him drop health, damage he takes
 * comes back at the enemy in proportion to the shield already stripped off it, and his shots pull
 * attention onto him.
 *
 * All three are the same bet stated three ways: the Tank is only worth anything while he is the one
 * being shot at, so every effect pays out for being in the middle of it and nothing pays out for
 * playing safe.
 *
 * There is no cooldown and no charge cost here, and the inherited fields for both are ignored: this
 * is never activated. A passive holds no slot and cannot be selected — see UAbilityComponent's
 * passive channel, which is separate from the ability inventory for exactly that reason.
 */
UCLASS(BlueprintType)
class POLARITY_API UAbilityDefinition_TankPassive : public UAbilityDefinition
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Passive|Levels", meta = (TitleProperty = "KillDropRadius"))
	TArray<FTankPassiveLevelStats> Levels;

	/** What an enemy killed nearby drops. Left empty means the enemy's own HealthPickupClass is used,
	 *  which is almost always what is wanted: one kind of health pickup in the game. Set it only to
	 *  make the Tank's bonus drop visibly different from an ordinary one. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Passive")
	TSubclassOf<AHealthPickup> OverrideHealthPickupClass;

	virtual int32 GetMaxLevel() const override { return FMath::Max(1, Levels.Num()); }

	UFUNCTION(BlueprintPure, Category = "Tank Passive|Levels")
	FTankPassiveLevelStats GetStatsAtLevel(int32 Level) const;
};
