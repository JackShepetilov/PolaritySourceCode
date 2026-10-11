// WeaponMovementProfile.h
// How a gun in the hands changes the way its holder moves [author, 2026-10-08].
//
// Multipliers on top of the one UMovementSettings asset, never a second copy of it: what the gun
// does not change (the camera, the wall run, the footsteps) stays tuned in one place. One profile per
// weapon class of the game (pistol, SMG, machine gun...), shared by every gun of that class; a single
// gun may point at its own.
//
// The profile in use rides the move (UApexMovementComponent::MoveWeaponSlot), because the server
// learns of a weapon switch later than the client that made it, and a speed the two disagree about
// is a correction on every frame of the switch.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "WeaponMovementProfile.generated.h"

UCLASS(BlueprintType)
class POLARITY_API UWeaponMovementProfile : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:

	/** Walking and crouching speed. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Speed", meta = (ClampMin = "0.1", ClampMax = "2.0"))
	float WalkSpeedScale = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Speed", meta = (ClampMin = "0.1", ClampMax = "2.0"))
	float SprintSpeedScale = 1.0f;

	/** The speed cap while aiming. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Speed", meta = (ClampMin = "0.1", ClampMax = "2.0"))
	float ADSSpeedScale = 1.0f;

	/** The speed a slide ADDS on entry (SlideMinSpeedBurst / SlideMaxSpeedBurst). 0: a slide is
	 *  never faster than the run that started it. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Slide", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float SlideBoostScale = 1.0f;

	/** How fast a slide bleeds speed on flat ground (SlideFrictionPerSecond and
	 *  SlideFlatDeceleration). Distance on the flat is roughly (entry speed - SlideMinSpeed) /
	 *  friction, so twice this is half the slide. Slopes are gravity and are not scaled. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Slide", meta = (ClampMin = "0.05", ClampMax = "20.0"))
	float SlideFrictionScale = 1.0f;
};
