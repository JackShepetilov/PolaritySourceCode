// GrenadeDefinition_Flash.cpp

#include "Variant_Shooter/Grenades/GrenadeDefinition_Flash.h"
#include "Variant_Shooter/Grenades/GrenadeProjectile.h"
#include "Variant_Shooter/Grenades/GrenadeComponent.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "Variant_Shooter/Suppression/PlayerSuppressionComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"

UGrenadeDefinition_Flash::UGrenadeDefinition_Flash()
{
	// Short: the counter to an enemy that runs from what it sees coming.
	FuseTime = 1.6f;
	DangerRadius = 900.0f;
}

float UGrenadeDefinition_Flash::ComputeExposure(const FVector& Origin, const FVector& Eyes, const FVector& ViewDirection) const
{
	const float Distance = FVector::Dist(Origin, Eyes);
	if (Distance >= MaxRadius)
	{
		return 0.0f;
	}

	const float Near = Distance <= FullEffectRadius ? 1.0f
		: 1.0f - (Distance - FullEffectRadius) / FMath::Max(1.0f, MaxRadius - FullEffectRadius);

	// Straight at it: 1. Turned away: down to BehindFactor, evenly by the angle past FacingFullAngle.
	const FVector ToFlash = (Origin - Eyes).GetSafeNormal();
	const float Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(ViewDirection.GetSafeNormal(), ToFlash), -1.0f, 1.0f)));
	const float Facing = Angle <= FacingFullAngle ? 1.0f
		: FMath::Lerp(1.0f, BehindFactor, FMath::Clamp((Angle - FacingFullAngle) / FMath::Max(1.0f, 180.0f - FacingFullAngle), 0.0f, 1.0f));

	return FMath::Clamp(Near * Facing, 0.0f, 1.0f);
}

void UGrenadeDefinition_Flash::Detonate(AGrenadeProjectile* Grenade, const FVector& Origin) const
{
	UWorld* const World = Grenade ? Grenade->GetWorld() : nullptr;
	if (!World || !Grenade->HasAuthority())
	{
		return;
	}

	FCollisionObjectQueryParams Objects(ECC_Pawn);
	FCollisionQueryParams OverlapParams(SCENE_QUERY_STAT(FlashOverlap), false, Grenade);
	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(Overlaps, Origin, FQuat::Identity, Objects,
		FCollisionShape::MakeSphere(FMath::Max(MaxRadius, PlayerLoudRadius)), OverlapParams);

	AActor* const Thrower = Grenade->GetInstigator();
	TSet<APawn*> Seen;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		APawn* const Pawn = Cast<APawn>(Overlap.GetActor());
		if (!Pawn || Seen.Contains(Pawn))
		{
			continue;
		}
		Seen.Add(Pawn);

		// Where the eyes are and where they look: a player's camera, an enemy's head.
		FVector Eyes;
		FRotator View;
		Pawn->GetActorEyesViewPoint(Eyes, View);
		const float Distance = FVector::Dist(Origin, Eyes);

		// Does the light reach the eyes. The sound gets through a wall, weaker.
		FCollisionQueryParams LineParams(SCENE_QUERY_STAT(FlashLine), false, Grenade);
		LineParams.AddIgnoredActor(Pawn);
		FHitResult Blocked;
		const bool bWall = World->LineTraceSingleByChannel(Blocked, Origin, Eyes, ECC_Visibility, LineParams);

		if (AShooterNPC* const NPC = Cast<AShooterNPC>(Pawn))
		{
			const float Exposure = bWall ? 0.0f : ComputeExposure(Origin, Eyes, View.Vector());
			if (Exposure >= MinExposure)
			{
				const float Duration = FMath::Lerp(MinStunDuration, MaxStunDuration, Exposure);
				NPC->ApplyFlashbang(Duration, BlindSpreadMultiplier, Thrower);
				UE_LOG(LogTemp, Log, TEXT("[GRENADE_DEBUG] flash: %s exposure %.2f at %.0f cm"), *GetNameSafe(Pawn), Exposure, Distance);
			}
			continue;
		}

		UGrenadeComponent* const Grenades = Pawn->FindComponentByClass<UGrenadeComponent>();
		if (!Grenades)
		{
			continue;
		}

		// The eyes: only if he looked at it. Closer is stronger, and so is nearer the middle of the view.
		float Burn = 0.0f;
		if (!bWall && Distance < MaxRadius)
		{
			const FVector ToFlash = (Origin - Eyes).GetSafeNormal();
			const float Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(View.Vector(), ToFlash), -1.0f, 1.0f)));
			if (Angle <= PlayerLookAngle)
			{
				const float Near = Distance <= FullEffectRadius ? 1.0f
					: 1.0f - (Distance - FullEffectRadius) / FMath::Max(1.0f, MaxRadius - FullEffectRadius);
				const float Centre = FMath::Lerp(1.0f, 0.5f, Angle / FMath::Max(1.0f, PlayerLookAngle));
				Burn = FMath::Clamp(Near * Centre, 0.0f, 1.0f);
			}
		}
		if (Burn < MinExposure)
		{
			Burn = 0.0f;
		}

		// The ears: whatever he looked at, if he was close.
		float Loud = PlayerLoudRadius > 0.0f ? FMath::Clamp(1.0f - Distance / PlayerLoudRadius, 0.0f, 1.0f) : 0.0f;
		if (bWall)
		{
			Loud *= WallMuffling;
		}
		if (Loud < 0.05f)
		{
			Loud = 0.0f;
		}

		if (Burn <= 0.0f && Loud <= 0.0f)
		{
			continue;
		}

		if (Loud > 0.0f)
		{
			if (UPlayerSuppressionComponent* const Suppression = Pawn->FindComponentByClass<UPlayerSuppressionComponent>())
			{
				Suppression->AddSuppression(Loud * PlayerSuppressionAtFull);
			}
		}

		FGrenadeFlashHit Hit;
		Hit.FlashLocation = Origin;
		Hit.Burn = Burn;
		Hit.BurnTime = Burn > 0.0f ? FMath::Lerp(PlayerBurnMinTime, PlayerBurnMaxTime, Burn) : 0.0f;
		Hit.BurnSize = FMath::Lerp(PlayerBurnMinSize, PlayerBurnMaxSize, Burn);
		Hit.Loud = Loud;
		Hit.DeafTime = PlayerDeafMaxTime * Loud;
		Hit.BurnMaterial = PlayerBurnMaterial;
		Hit.DeafMix = PlayerDeafMix;
		Hit.Ring = PlayerRingSound;
		Grenades->Client_Flashbang(Hit);

		UE_LOG(LogTemp, Log, TEXT("[GRENADE_DEBUG] flash: player %s burn %.2f (%.1f s) loud %.2f (%.1f s) at %.0f cm%s"),
			*GetNameSafe(Pawn), Burn, Hit.BurnTime, Loud, Hit.DeafTime, Distance, bWall ? TEXT(", behind a wall") : TEXT(""));
	}
}
