// ShooterWeapon_Shotgun.h
// One trigger pull, several pellets, one round out of the magazine.

#pragma once

#include "CoreMinimal.h"
#include "ShooterWeapon.h"
#include "ShooterWeapon_Shotgun.generated.h"

/**
 * A shotgun: the shot is a group of pellets that leave the barrel together and are traced
 * separately, so damage is decided by how many of them land rather than by one hit or miss.
 *
 * The pattern is FIXED, not random, and that is the whole design. A random cone makes a shotgun a
 * dice roll at every range; a fixed pattern makes it a skill: the player learns the shape, learns
 * how wide it is at what distance, and aims so that all of it lands. Randomness is still available
 * without touching this class -- AimVariance in the base wanders the whole pattern around the
 * crosshair, which is a different (and honest) thing from each pellet going somewhere else.
 *
 * The defaults describe the Apex Mozambique, because that is what this was built for: three pellets
 * in a triangle, 17 damage each, a five-round magazine and a slow automatic cadence. Everything is
 * a property, so a second, wider, eight-pellet shotgun is a Blueprint, not another C++ class.
 *
 * What one shot costs is unchanged: pellets are not rounds. The montage plays once, the recoil kicks
 * once, one round leaves the magazine (see AShooterWeapon::ConsumeRoundAfterShot).
 */
UCLASS()
class POLARITY_API AShooterWeapon_Shotgun : public AShooterWeapon
{
	GENERATED_BODY()

public:

	AShooterWeapon_Shotgun();

	/** How many pellets one trigger pull puts in the air. */
	UFUNCTION(BlueprintPure, Category = "Shotgun")
	int32 GetPelletCount() const { return PelletPattern.Num(); }

	/** Half-angle of the pattern, in degrees: the offset of a pellet at the edge of it. */
	UFUNCTION(BlueprintPure, Category = "Shotgun")
	float GetPelletSpreadAngle() const { return PelletSpreadAngle; }

protected:

	// ==================== Pattern ====================

	/** Where each pellet goes, as an offset from the aim line on a unit circle: X to the right,
	 *  Y up, both scaled by PelletSpreadAngle. One entry per pellet.
	 *
	 *  Kept unit-length rather than in degrees so the shape and the size of the pattern are two
	 *  separate decisions: tighten the whole thing with one number without redrawing the triangle.
	 *  An empty array falls back to the base class, i.e. one pellet straight down the aim line. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shotgun")
	TArray<FVector2D> PelletPattern;

	/** How far out the edge of the pattern sits, as an angle. Degrees, because a spread that is an
	 *  angle keeps its shape at every range: 2 degrees is ~35 cm across at 10 m and ~70 cm at 20 m. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shotgun", meta = (ClampMin = "0.0", ClampMax = "45.0", Units = "deg"))
	float PelletSpreadAngle = 2.0f;

	// ==================== Firing ====================

	virtual void FireHitscan(const FVector& TargetLocation) override;
	virtual void FireProjectile(const FVector& TargetLocation, float ChargeMultiplier = 1.0f) override;

	/** The aim line turned by one pattern offset. Returns the aim line unchanged for a centre
	 *  pellet or a zero spread. */
	FVector GetPelletDirection(const FVector& AimDirection, const FVector2D& PatternOffset) const;
};
