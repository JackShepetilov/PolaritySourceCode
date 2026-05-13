// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "Upgrade_AirKick.h"
#include "UpgradeDefinition_AirKick.h"
#include "ShooterCharacter.h"
#include "EMFPhysicsProp.h"
#include "MeleeAttackComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"

UUpgrade_AirKick::UUpgrade_AirKick()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UUpgrade_AirKick::OnUpgradeActivated()
{
	DefAirKick = Cast<UUpgradeDefinition_AirKick>(UpgradeDefinition);
	if (!DefAirKick.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("Air Kick: UpgradeDefinition is not UUpgradeDefinition_AirKick!"));
		return;
	}

	AShooterCharacter* Character = GetShooterCharacter();
	if (!Character)
	{
		return;
	}

	UMeleeAttackComponent* MeleeComp = Character->FindComponentByClass<UMeleeAttackComponent>();
	if (MeleeComp)
	{
		MeleeComp->OnMeleeHit.AddDynamic(this, &UUpgrade_AirKick::HandleMeleeHit);
	}
}

void UUpgrade_AirKick::OnUpgradeDeactivated()
{
	AShooterCharacter* Character = GetShooterCharacter();
	if (!Character)
	{
		return;
	}

	UMeleeAttackComponent* MeleeComp = Character->FindComponentByClass<UMeleeAttackComponent>();
	if (MeleeComp)
	{
		MeleeComp->OnMeleeHit.RemoveDynamic(this, &UUpgrade_AirKick::HandleMeleeHit);
	}
}

void UUpgrade_AirKick::HandleMeleeHit(AActor* HitActor, const FVector& HitLocation, bool bHeadshot, float Damage)
{
	if (!DefAirKick.IsValid() || !HitActor)
	{
		return;
	}

	// Must be hitting an EMF physics prop
	AEMFPhysicsProp* Prop = Cast<AEMFPhysicsProp>(HitActor);
	if (!Prop)
	{
		return;
	}

	AShooterCharacter* Character = GetShooterCharacter();
	if (!Character)
	{
		return;
	}

	// Player must be airborne
	UCharacterMovementComponent* MovementComp = Character->GetCharacterMovement();
	if (!MovementComp || !MovementComp->IsFalling())
	{
		return;
	}

	// Prop must be airborne (no ground within trace distance)
	if (!IsPropAirborne(Prop))
	{
		return;
	}

	// === All conditions met: launch the prop, primed (or not) per current level ===

	const FAirKickLevelData& LD = DefAirKick->GetLevelData(CurrentLevel);

	UE_LOG(LogTemp, Warning, TEXT("[AIR_KICK_DEBUG] %s air-kicked %s — Lv%d/%d, launch %.0fcm/s, ExplodeOnImpact=%d, dmg=%.1f, radius=%.0f"),
		*Character->GetName(), *Prop->GetName(),
		CurrentLevel, DefAirKick->MaxLevel,
		DefAirKick->LaunchSpeed,
		LD.bExplodeOnImpact ? 1 : 0,
		LD.FixedExplosionDamage,
		LD.FixedExplosionRadius);

	// Only prime the explosion params if this level actually wants an explosion on impact.
	// Level 1 (no explosion) keeps the prop's own settings intact — kick becomes a plain
	// physics launch, just like the old behaviour. Level 2 (or higher) overrides the prop
	// to detonate with our fixed damage / radius regardless of its own configuration.
	if (LD.bExplodeOnImpact)
	{
		// Overrides (see commit history for rationale):
		//   - bCanExplode = true: even non-explosive props blow up on impact.
		//   - ExplosionDamage / ExplosionRadius: predictable, level-defined payload.
		//   - bScaleExplosionWithCharge = false: damage doesn't ride on prop charge.
		//   - ExplosionMinCharge = 0: prevents fallback to "weak impact" (which deals
		//     WeakImpactDamage instead of running Explode at all).
		//   - ExplosionDamageFalloff = 5 (max by UPROPERTY clamp): keeps damage near-flat
		//     across the whole blast radius (only the inner 30% gets full damage by default).
		Prop->bCanExplode = true;
		Prop->ExplosionDamage = LD.FixedExplosionDamage;
		Prop->ExplosionRadius = LD.FixedExplosionRadius;
		Prop->bScaleExplosionWithCharge = false;
		Prop->ExplosionMinCharge = 0.0f;
		Prop->ExplosionDamageFalloff = 5.0f;
	}

	// Launch the prop in the camera forward direction (same as the previous version).
	APlayerController* PC = Cast<APlayerController>(Character->GetController());
	FVector LaunchDir = Character->GetActorForwardVector(); // fallback
	if (PC)
	{
		FVector ViewLocation;
		FRotator ViewRotation;
		PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
		LaunchDir = ViewRotation.Vector();
	}

	UStaticMeshComponent* PropMesh = Prop->PropMesh;
	if (PropMesh && PropMesh->IsSimulatingPhysics())
	{
		PropMesh->SetPhysicsLinearVelocity(LaunchDir * DefAirKick->LaunchSpeed);

		if (DefAirKick->KickSpinSpeed > 0.0f)
		{
			const FVector RandomAxis = FMath::VRand();
			PropMesh->SetPhysicsAngularVelocityInDegrees(RandomAxis * DefAirKick->KickSpinSpeed);
		}
	}

	// Optional kick-specific cosmetic feedback at the contact point.
	if (DefAirKick->KickImpactFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			GetWorld(),
			DefAirKick->KickImpactFX,
			HitLocation,
			LaunchDir.Rotation(),
			FVector(DefAirKick->KickImpactFXScale),
			true,  // auto-destroy
			true,  // auto-activate
			ENCPoolMethod::None
		);
	}

	if (DefAirKick->KickSound)
	{
		UGameplayStatics::PlaySoundAtLocation(
			GetWorld(),
			DefAirKick->KickSound,
			HitLocation,
			DefAirKick->KickSoundVolume,
			DefAirKick->KickSoundPitch
		);
	}
}

bool UUpgrade_AirKick::IsPropAirborne(AActor* Prop) const
{
	if (!Prop || !DefAirKick.IsValid())
	{
		return false;
	}

	const float TraceDistance = DefAirKick->PropAirborneTraceDistance;
	const FVector PropLocation = Prop->GetActorLocation();
	const FVector TraceStart = PropLocation;
	const FVector TraceEnd = PropLocation - FVector(0.0f, 0.0f, TraceDistance);

	FHitResult HitResult;
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(Prop);
	// Also ignore the player
	if (AShooterCharacter* Character = GetShooterCharacter())
	{
		QueryParams.AddIgnoredActor(Character);
	}

	const bool bHitGround = GetWorld()->LineTraceSingleByChannel(
		HitResult,
		TraceStart,
		TraceEnd,
		ECC_WorldStatic,
		QueryParams
	);

	return !bHitGround;
}
