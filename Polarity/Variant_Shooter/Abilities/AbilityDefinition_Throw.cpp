// AbilityDefinition_Throw.cpp

#include "AbilityDefinition_Throw.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/GameplayStaticsTypes.h"

FAbilityCommonStats UAbilityDefinition_Throw::GetCommonStatsAtLevel(int32 Level) const
{
	FAbilityCommonStats Common;
	Common.Cooldown = ThrowCooldown;
	Common.MinimumChargeToActivate = ThrowMinimumCharge;
	return Common;
}

void UAbilityDefinition_Throw::GetThrowStart(const AShooterCharacter* Caster, FVector& OutLocation,
	FRotator& OutRotation) const
{
	OutLocation = FVector::ZeroVector;
	OutRotation = FRotator::ZeroRotator;
	if (!Caster)
	{
		return;
	}

	// The character's own aim ray, the one the crosshair sits on. Not GetPawnViewLocation(): that is
	// the capsule plus eye height, and the camera has been moved by crouch, lag and ADS.
	// @see AShooterCharacter::GetAimRay
	FVector Eye, AimEnd;
	Caster->GetAimRay(AimConvergeDistance, Eye, AimEnd);
	const FRotator Aim = (AimEnd - Eye).Rotation();
	OutLocation = Eye + Aim.RotateVector(ThrowOriginOffset);

	// With a shoulder against a wall the hand point can be on the far side of it, and a payload
	// born there would be thrown from inside the wall. Fall back to the eyes.
	if (const UWorld* World = Caster->GetWorld())
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(ThrowOrigin), false, Caster);
		FHitResult Blocked;
		if (World->LineTraceSingleByChannel(Blocked, Eye, OutLocation, ECC_Visibility, Params))
		{
			OutLocation = Eye;
		}
	}

	// Aimed at a point down the crosshair, not parallel to it: the start is off to one side.
	OutRotation = (AimEnd - OutLocation).Rotation();
	OutRotation.Pitch = FMath::Clamp(OutRotation.Pitch + ThrowPitchOffset, -89.0f, 89.0f);
}

bool UAbilityDefinition_Throw::PredictThrowPath(const AShooterCharacter* Caster, TArray<FVector>& OutPoints,
	FHitResult& OutHit) const
{
	OutPoints.Reset();
	OutHit = FHitResult();

	const UWorld* World = Caster ? Caster->GetWorld() : nullptr;
	if (!World)
	{
		return false;
	}

	FVector Start;
	FRotator Rotation;
	GetThrowStart(Caster, Start, Rotation);

	FPredictProjectilePathParams Params(PreviewRadius, Start, Rotation.Vector() * GetThrowSpeed(),
		PreviewMaxTime, ECC_Visibility, const_cast<AShooterCharacter*>(Caster));
	Params.OverrideGravityZ = World->GetGravityZ() * GetThrowGravityScale();
	Params.SimFrequency = 20.0f;

	FPredictProjectilePathResult Result;
	const bool bHit = UGameplayStatics::PredictProjectilePath(Caster, Params, Result);

	OutPoints.Reserve(Result.PathData.Num());
	for (const FPredictProjectilePathPointData& Point : Result.PathData)
	{
		OutPoints.Add(Point.Location);
	}
	OutHit = Result.HitResult;
	return bHit;
}
