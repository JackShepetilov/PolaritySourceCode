// ShooterWeapon_Shotgun.h
// One trigger pull, several pellets, one round out of the magazine.

#pragma once

#include "CoreMinimal.h"
#include "ShooterWeapon.h"
#include "ShooterWeapon_Shotgun.generated.h"

/**
 * How the generator lays pellets out inside the spread circle.
 *
 * These two are not variations on a theme, they are different things, which is why this is a choice
 * and not a formula. A rim pattern is an arcade shotgun: a small, fixed, readable shape (three
 * pellets on a rim IS the Mozambique triangle). Real buckshot fills the disc, and a rim-only layout
 * of eight pellets is a doughnut whose middle -- exactly where the player is aiming -- does nothing.
 */
UENUM(BlueprintType)
enum class EPelletPatternLayout : uint8
{
	/** Every pellet on the rim of the circle, evenly spaced, first one at the top. */
	Ring,

	/** Spread over the whole circle so the density is even by AREA: a pellet in the middle, one at
	 *  the rim, and the rest between them on a sunflower spiral. This is the one that behaves like
	 *  a shotgun. */
	FilledDisc
};

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

	/** Where each pellet goes, as an offset from the aim line inside the unit circle: X to the right,
	 *  Y up, both scaled by PelletSpreadAngle. One entry per pellet.
	 *
	 *  Kept as a fraction of the spread rather than in degrees so the shape and the size of the pattern are two
	 *  separate decisions: tighten the whole thing with one number without redrawing the triangle.
	 *  An empty array falls back to the base class, i.e. one pellet straight down the aim line. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shotgun")
	TArray<FVector2D> PelletPattern;

	/** How far out the edge of the pattern sits, as an angle. Degrees, because a spread that is an
	 *  angle keeps its shape at every range: 2 degrees is ~35 cm across at 10 m and ~70 cm at 20 m. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shotgun", meta = (ClampMin = "0.0", ClampMax = "45.0", Units = "deg"))
	float PelletSpreadAngle = 2.0f;

	/** How far each pellet is allowed to wander from its place in the pattern, per shot, as a
	 *  fraction of the spread radius. 0.2 means a fifth of the way to the rim.
	 *
	 *  Zero keeps the pattern exactly as authored, which is the fixed-pattern design this class was
	 *  built around: the player learns one shape. Turning it up trades that for the way real
	 *  buckshot behaves, where no two shots print the same. It is a fraction rather than an angle on
	 *  purpose, so tightening PelletSpreadAngle tightens the wander with it instead of leaving a
	 *  weapon whose scatter is suddenly wider than its own pattern. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shotgun", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float PelletJitter = 0.0f;

	// ==================== Pattern generator ====================
	//
	// The pattern above is authored by hand, entry by entry, and stays that way: the shape of a
	// shotgun's spread is a decision, not a formula. This is the shortcut for the ordinary case --
	// N pellets spaced evenly around the aim line -- and it writes into that same array, so a
	// generated pattern can then be dragged about by hand exactly like a hand-made one.

	/** How many pellets Generate Pellet Pattern lays out. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shotgun|Pattern Generator", meta = (ClampMin = "1", ClampMax = "64"))
	int32 GeneratedPelletCount = 3;

	/** Rim or filled disc. Left on Ring so pressing the button on this weapon still produces the
	 *  triangle it ships with; a shotgun with more than a handful of pellets wants FilledDisc. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shotgun|Pattern Generator")
	EPelletPatternLayout GeneratedPatternLayout = EPelletPatternLayout::Ring;

	/** Replaces PelletPattern with GeneratedPelletCount pellets spaced evenly around the aim line,
	 *  the first one at the top. For three that is exactly the triangle this class ships with, so
	 *  pressing it does not quietly change a weapon that was already correct. */
	UFUNCTION(CallInEditor, Category = "Shotgun|Pattern Generator")
	void GeneratePelletPattern();

	// ==================== Firing ====================

	virtual void FireHitscan(const FVector& TargetLocation) override;
	virtual void FireProjectile(const FVector& TargetLocation, float ChargeMultiplier = 1.0f) override;

	/** The aim line turned by one pattern offset. Returns the aim line unchanged for a centre
	 *  pellet or a zero spread. */
	FVector GetPelletDirection(const FVector& AimDirection, const FVector2D& PatternOffset) const;
};
