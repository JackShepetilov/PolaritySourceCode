// CrosshairConfig.h
// Per-weapon HUD crosshair appearance + cosmetic bloom. Plain data struct (no UMG / weapon
// dependency) so both AShooterWeapon (owns one per weapon) and UCrosshairWidget (reads it) can
// include just this header.
//
// Bloom is COSMETIC and applied as a SIZE multiplier on the single crosshair image: the crosshair
// grows while firing (and optionally moving / airborne) and settles back. Set BloomScaleAdd = 0 for
// a fully static crosshair. Base size is normalized (fraction of viewport height) so it's
// resolution-independent; Scale is the per-weapon size multiplier at rest.

#pragma once

#include "CoreMinimal.h"
#include "CrosshairConfig.generated.h"

class UTexture2D;

USTRUCT(BlueprintType)
struct FCrosshairConfig
{
	GENERATED_BODY()

	// ==================== Appearance ====================

	/** Crosshair texture for this weapon. Light/white art tints cleanly via Color. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair")
	TObjectPtr<UTexture2D> Image = nullptr;

	/** Tint applied to the crosshair. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair")
	FLinearColor Color = FLinearColor::White;

	/** Normalized size multiplier at rest. 1.0 = normal, >1 bigger, <1 smaller. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair", meta = (ClampMin = "0.1"))
	float Scale = 1.0f;

	// ==================== Procedural ticks ====================
	//
	// The crosshair as four bars drawn by code, the way Apex and most modern shooters do it, rather
	// than one texture stretched to size.
	//
	// The difference is not cosmetic. A stretched texture scales EVERYTHING: open the spread and the
	// bars themselves get longer and fatter, so the crosshair reads as a different crosshair in
	// every state and the thin lines turn to mush at the top end. Bars drawn by code keep their
	// length and their weight, and only the GAP moves. The gap is then free to mean exactly one
	// thing: the inner end of each bar sits on the edge of the spread cone.
	//
	// It also ends the calibration problem outright. There is no art whose ring might sit at 70% of
	// its own texture, so there is nothing to measure and no fudge factor to get wrong: the bars are
	// placed at the angle, full stop.

	/** Draw the crosshair as four code-drawn bars. Off falls back to the Blueprint's own Image,
	 *  driven through On Crosshair Resized as before. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair|Ticks")
	bool bDrawProceduralTicks = true;

	/** Length of one bar, outward from the gap. Layout units, so it follows the UI's DPI scaling and
	 *  keeps its apparent size at any resolution -- unlike the gap, which is a real angle and must
	 *  not. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair|Ticks", meta = (ClampMin = "1.0"))
	float TickLength = 9.0f;

	/** Thickness of one bar. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair|Ticks", meta = (ClampMin = "1.0"))
	float TickThickness = 2.0f;

	/** Which of the four bars are drawn. Dropping the top one is the common choice for a weapon
	 *  whose recoil climbs, so the crosshair stops covering what the player is climbing into. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair|Ticks")
	bool bTickTop = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair|Ticks")
	bool bTickBottom = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair|Ticks")
	bool bTickLeft = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair|Ticks")
	bool bTickRight = true;

	/** Side of the centre dot. 0 turns it off. The dot does NOT move with the spread: it marks where
	 *  the barrel points, which is the one thing the spread does not change. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair|Ticks", meta = (ClampMin = "0.0"))
	float CenterDotSize = 2.0f;

	/** Dark border drawn under the bars so they stay readable against a bright wall or the sky.
	 *  0 turns it off. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair|Ticks", meta = (ClampMin = "0.0"))
	float OutlineThickness = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair|Ticks")
	FLinearColor OutlineColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.8f);

	// ==================== Spread-driven size ====================
	//
	// The honest mode: the crosshair is the weapon's spread cone projected onto the screen, so its
	// width IS the width of the region a shot can land in, at any FOV and any resolution. The Bloom
	// block below is the older cosmetic path and is only used when this is off.

	/** Size the crosshair from the weapon's real spread (AShooterWeapon::GetCrosshairSpreadDegrees)
	 *  instead of the cosmetic bloom. Turning this off restores the old grow-on-fire behaviour. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair|Spread")
	bool bSizeFromSpread = true;

	/** Multiplies the projected size. 1.0 draws the cone exactly; a little over 1 is the usual
	 *  choice so the art sits just outside the region rather than on top of it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair|Spread", meta = (ClampMin = "0.1"))
	float SpreadSizeMultiplier = 1.0f;

	/** Floor on the on-screen size, so a pinpoint weapon still has a crosshair to aim with. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair|Spread", meta = (ClampMin = "0.0"))
	float MinSizePixels = 18.0f;

	/** Ceiling on the on-screen size, so a very wide spread cannot grow off the screen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair|Spread", meta = (ClampMin = "1.0"))
	float MaxSizePixels = 600.0f;

	/** How fast the drawn size follows the real spread (interp speed, per second). The spread itself
	 *  already interpolates; this only stops single-frame jitter. Raise it toward 60 for a crosshair
	 *  that snaps exactly with the number. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair|Spread", meta = (ClampMin = "0.1"))
	float SpreadFollowSpeed = 25.0f;

	// ==================== Aiming down sights ====================

	/** Hide the crosshair while aiming down sights: the sight on the weapon is the aim now, and a
	 *  hip crosshair floating over it is two answers to the same question. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair|ADS")
	bool bHideWhileAiming = true;

	/** ADS alpha at which the crosshair goes away. Below 1 so it clears before the sight arrives. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair|ADS", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HideAtADSAlpha = 0.35f;

	// ==================== Bloom (grow on fire / move / air) ====================

	/** Extra size at full bloom. 0.6 = up to 60% bigger; 0 = fully static. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair|Bloom", meta = (ClampMin = "0.0"))
	float BloomScaleAdd = 0.6f;

	/** Bloom added while firing (0..1). This is the "grows when shooting" part. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair|Bloom", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FireBloom = 1.0f;

	/** Bloom added at full movement speed (0..1), scaled by current speed. Default 0 (off). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair|Bloom", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MoveBloom = 0.0f;

	/** Bloom added while airborne (0..1). Default 0 (off). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair|Bloom", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AirBloom = 0.0f;

	/** How fast the crosshair grows toward its target (interp speed, per second). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair|Bloom", meta = (ClampMin = "0.1"))
	float BloomAttackSpeed = 14.0f;

	/** How fast it settles back to rest (interp speed, per second). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair|Bloom", meta = (ClampMin = "0.1"))
	float BloomRecoverySpeed = 7.0f;
};
