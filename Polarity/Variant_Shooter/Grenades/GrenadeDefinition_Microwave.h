// GrenadeDefinition_Microwave.h
// The microwave: a headache through walls over a wide area. It does not stun, it makes soldiers dull.

#pragma once

#include "CoreMinimal.h"
#include "Variant_Shooter/Grenades/GrenadeDefinition.h"
#include "GrenadeDefinition_Microwave.generated.h"

/**
 * [author, 2026-10-14] The flash is quick and stuns for a few seconds; the microwave makes the
 * soldiers dull: the squad stops talking, aim and reaction get worse, they stop holding angles. It
 * goes through walls and reaches further than the flash: thrown into a building, or over a stretch
 * of open ground. What it buys a careful player: kill the first one, and the second freezes for a
 * moment when his friend falls next to him.
 *
 * Everything it does to a soldier is UAIAccuracyComponent's headache (ApplyHeadache): stress no lower
 * than the strength, reaction x HeadacheReactionMultiplier, no holding and no first volley, no
 * callouts, a freeze when a friend falls near. This asset decides only who gets how much, for how long.
 *
 * It is an emitter, not a bang [author, 2026-10-14]: once the fuse is out it lies there working for
 * ActiveSeconds, a pulse every PulseInterval, and every pulse renews the headache of everybody in
 * reach, the ones who walk in late as well.
 *
 * The core [author, 2026-10-15]: inside CoreRadius it is unbearable. The soldiers there back out of the
 * core at a walk (a headache takes the sprint and the slide away) shooting at the thing, with a
 * comical aim (WorkingHitChance a round); HitsToBreak hits break it, so it takes more than three of
 * them stuck in the core to do it. Out of the core they leave it alone and fight on, dull. Nobody is
 * driven out of the whole reach any more.
 *
 * Players in reach get it too, the thrower as well, as with the flash: the sight sways and comes up
 * slower, the picture throbs at the edges, no sprint and no slide while it holds.
 * @see UPlayerSuppressionComponent::ApplyHeadache
 */
UCLASS(BlueprintType)
class POLARITY_API UGrenadeDefinition_Microwave : public UGrenadeDefinition
{
	GENERATED_BODY()

public:

	UGrenadeDefinition_Microwave();

	/** Full strength up to here (cm)... */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Microwave", meta = (ClampMin = "0.0", Units = "cm"))
	float FullEffectRadius = 1000.0f;

	/** ...falling to EdgeStrength / EdgeSeconds here, and nothing past it (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Microwave", meta = (ClampMin = "0.0", Units = "cm"))
	float MaxRadius = 2000.0f;

	/** Headache strength in the full radius, 0 to 1: the stress it holds the soldier at. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Microwave", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Strength = 0.85f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Microwave", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float EdgeStrength = 0.45f;

	/** How long the headache lasts after the last pulse that reached it, in the full radius (s)... */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Microwave", meta = (ClampMin = "0.0", Units = "s"))
	float Seconds = 6.0f;

	/** ...and at MaxRadius (s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Microwave", meta = (ClampMin = "0.0", Units = "s"))
	float EdgeSeconds = 3.0f;

	/** How long it works after the fuse is out (s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Microwave|Emitter", meta = (ClampMin = "0.0", Units = "s"))
	float ActiveSeconds = 6.0f;

	/** A pulse every this many seconds while it works (s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Microwave|Emitter", meta = (ClampMin = "0.05", Units = "s"))
	float PulseInterval = 0.5f;

	/** The core: soldiers inside it back out of it, shooting at the grenade (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Microwave|Core", meta = (ClampMin = "0.0", Units = "cm"))
	float CoreRadius = 600.0f;

	/** The chance a round aimed at the working grenade lands. Comical on purpose: three soldiers in
	 *  the core for the whole six seconds come to about 5.4 hits. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Microwave|Core", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WorkingHitChance = 0.03f;

	/** Hits that break it (counted, not damage: a shotgun and a rifle round are one hit each). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Microwave|Core", meta = (ClampMin = "0"))
	int32 HitsToBreak = 6;

	/** Off: walls stop nothing, the point of it. On: only those with a clear line to it, like the flash. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Microwave")
	bool bStoppedByWalls = false;

	virtual void Detonate(AGrenadeProjectile* Grenade, const FVector& Origin) const override;
	virtual void Pulse(AGrenadeProjectile* Grenade, const FVector& Origin) const override;
	virtual float GetActiveSeconds() const override { return ActiveSeconds; }
	virtual float GetPulseInterval() const override { return PulseInterval; }
	virtual float GetWorkingDangerRadius() const override { return CoreRadius; }
	virtual float GetWorkingEffectRadius() const override { return MaxRadius; }
	virtual bool DoWallsProtect() const override { return bStoppedByWalls; }
	virtual bool IsShotAtWhileWorking() const override { return true; }
	virtual float GetWorkingHitChance() const override { return WorkingHitChance; }
	virtual int32 GetHitsToBreak() const override { return HitsToBreak; }

private:

	/** One pulse: the headache to everybody in reach, soldiers and players. Returns how many soldiers. */
	int32 ApplyPulse(AGrenadeProjectile* Grenade, const FVector& Origin, bool bLog) const;
};
