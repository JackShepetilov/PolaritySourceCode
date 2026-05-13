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

	// === All conditions met: launch the prop, primed to detonate on NPC impact ===

	UE_LOG(LogTemp, Warning, TEXT("[AIR_KICK_DEBUG] %s air-kicked %s — launch %.0fcm/s, primed explosion dmg=%.1f"),
		*Character->GetName(), *Prop->GetName(),
		DefAirKick->LaunchSpeed,
		DefAirKick->FixedExplosionDamage);

	// Prime the prop's explosion params BEFORE launching, so when it hits an NPC
	// mid-flight it detonates with our fixed damage instead of the prop's own
	// configured damage / charge-scaled damage. Overrides:
	//   - bCanExplode = true: even non-explosive props blow up on impact.
	//   - ExplosionDamage = FixedExplosionDamage: predictable payload.
	//   - bScaleExplosionWithCharge = false: damage doesn't ride on prop charge.
	// We don't restore these — after Explode() the prop is normally
	// destroyed/hidden, and if it survives, the override staying in place is
	// acceptable for subsequent chain reactions.
	Prop->bCanExplode = true;
	Prop->ExplosionDamage = DefAirKick->FixedExplosionDamage;
	Prop->bScaleExplosionWithCharge = false;

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
