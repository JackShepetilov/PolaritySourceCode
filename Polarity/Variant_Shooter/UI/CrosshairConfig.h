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
