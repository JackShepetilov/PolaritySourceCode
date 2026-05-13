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

	// === All conditions met: force-detonate the prop with fixed damage ===

	UE_LOG(LogTemp, Warning, TEXT("[AIR_KICK_DEBUG] %s air-kicked %s — forcing explosion (fixed dmg=%.1f, radiusMul=%.2f, vfxMul=%.2f)"),
		*Character->GetName(), *Prop->GetName(),
		DefAirKick->FixedExplosionDamage,
		DefAirKick->ExplosionRadiusMultiplier,
		DefAirKick->ExplosionVFXScaleMultiplier);

	// Override the prop's explosion parameters so the kick always detonates with
	// the upgrade's fixed damage, regardless of:
	//  - the prop's bCanExplode flag (non-explosive props still detonate)
	//  - the prop's own ExplosionDamage value
	//  - charge-based scaling (bScaleExplosionWithCharge)
	// We cache the originals and restore them after Explode() in case the prop
	// survives detonation (some props stay alive after explode for chain reactions).
	const bool   bSavedCanExplode  = Prop->bCanExplode;
	const float  SavedExplosionDmg = Prop->ExplosionDamage;
	const bool   bSavedScaleWithCharge = Prop->bScaleExplosionWithCharge;

	Prop->bCanExplode = true;
	Prop->ExplosionDamage = DefAirKick->FixedExplosionDamage;
	Prop->bScaleExplosionWithCharge = false;

	// Damage multiplier kept at 1.0 — fixed damage was set on the prop directly above.
	Prop->Explode(/*DamageMultiplier=*/ 1.0f,
		/*RadiusMultiplier=*/ DefAirKick->ExplosionRadiusMultiplier,
		/*VFXScaleMultiplier=*/ DefAirKick->ExplosionVFXScaleMultiplier);

	if (IsValid(Prop) && !Prop->IsActorBeingDestroyed())
	{
		Prop->bCanExplode = bSavedCanExplode;
		Prop->ExplosionDamage = SavedExplosionDmg;
		Prop->bScaleExplosionWithCharge = bSavedScaleWithCharge;
	}

	// Optional kick-specific cosmetic feedback on top of the prop's explosion VFX/sound.
	if (DefAirKick->KickImpactFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			GetWorld(),
			DefAirKick->KickImpactFX,
			HitLocation,
			FRotator::ZeroRotator,
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
