// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "Upgrade_AirKick.h"
#include "UpgradeDefinition_AirKick.h"
#include "ShooterCharacter.h"
#include "EMFPhysicsProp.h"
#include "MeleeAttackComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "EMFVelocityModifier.h"

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

	AShooterCharacter* Character = GetShooterCharacter();
	if (!Character)
	{
		return;
	}

	// Air Mail only triggers from a mid-air melee hit.
	UCharacterMovementComponent* MovementComp = Character->GetCharacterMovement();
	const bool bPlayerAirborne = MovementComp && MovementComp->IsFalling();

	// --- NPC branch: air-melee a max-charge enemy → launch it into the air ---
	// Reuses the reverse-channeling throw (AShooterNPC::LaunchIntoAir). Works on already-stunned
	// enemies; humanoids are immune (their bodies can't be electrified to max charge anyway).
	if (AShooterNPC* NPC = Cast<AShooterNPC>(HitActor))
	{
		if (bPlayerAirborne && IsNPCAtMaxCharge(NPC))
		{
			LaunchNPC(NPC, Character, HitLocation);
		}
		return; // NPCs never take the prop path below
	}

	// --- Prop branch (existing behavior): air-kick an airborne EMF physics prop ---
	AEMFPhysicsProp* Prop = Cast<AEMFPhysicsProp>(HitActor);
	if (!Prop)
	{
		return;
	}

	if (!bPlayerAirborne)
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

	UE_LOG(LogTemp, Warning, TEXT("[AIR_KICK_DEBUG] %s air-kicked %s — Lv%d/%d, launch %.0fcm/s, ExplodeOnImpact=%d, impact=%.1f, exploDmg=%.1f, radius=%.0f"),
		*Character->GetName(), *Prop->GetName(),
		CurrentLevel, DefAirKick->MaxLevel,
		DefAirKick->LaunchSpeed,
		LD.bExplodeOnImpact ? 1 : 0,
		LD.ImpactDamage,
		LD.FixedExplosionDamage,
		LD.FixedExplosionRadius);

	// Both branches force bCanExplode=true and disable charge-scaling so the prop's
	// impact behaviour is deterministic. Then we pick which path EMFPhysicsProp takes
	// at impact time by controlling ExplosionMinCharge:
	//   bExplodeOnImpact = true  -> set MinCharge=0  -> always run Explode() path (AoE)
	//   bExplodeOnImpact = false -> set MinCharge=∞  -> always run WeakImpact path
	//                                                   (single-target damage + bounce)
	Prop->bCanExplode = true;
	Prop->bScaleExplosionWithCharge = false;

	if (LD.bExplodeOnImpact)
	{
		// AoE explosion. ExplosionDamageFalloff=5 keeps damage near-flat across the
		// blast radius (default falloff would drop it to ~10% on the edge).
		Prop->ExplosionMinCharge = 0.0f;
		Prop->ExplosionDamage = LD.FixedExplosionDamage;
		Prop->ExplosionRadius = LD.FixedExplosionRadius;
		Prop->ExplosionDamageFalloff = 5.0f;
	}
	else
	{
		// Single-target "weak impact" — no explosion VFX, no radius, just a clean hit
		// on whoever the prop physically struck, then the prop bounces off and might
		// continue to hit another NPC.
		Prop->ExplosionMinCharge = 1e9f; // never reaches the explosion path
		Prop->WeakImpactDamage = LD.ImpactDamage;
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

bool UUpgrade_AirKick::IsNPCAtMaxCharge(AShooterNPC* NPC) const
{
	if (!NPC)
	{
		return false;
	}

	UEMFVelocityModifier* EMF = NPC->FindComponentByClass<UEMFVelocityModifier>();
	if (!EMF)
	{
		return false;
	}

	// Charge accumulation is clamped to MaxBaseCharge, so |GetCharge()| reaching it == "max charge".
	return FMath::Abs(EMF->GetCharge()) >= (EMF->MaxBaseCharge - KINDA_SMALL_NUMBER);
}

void UUpgrade_AirKick::LaunchNPC(AShooterNPC* NPC, AShooterCharacter* Character, const FVector& HitLocation) const
{
	if (!NPC || !Character || !DefAirKick.IsValid())
	{
		return;
	}

	// Launch direction: camera-forward blended toward straight up by NPCLaunchUpBias.
	FVector ForwardDir = Character->GetActorForwardVector();
	if (APlayerController* PC = Cast<APlayerController>(Character->GetController()))
	{
		FVector ViewLocation;
		FRotator ViewRotation;
		PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
		ForwardDir = ViewRotation.Vector();
	}

	const float UpBias = FMath::Clamp(DefAirKick->NPCLaunchUpBias, 0.0f, 1.0f);
	FVector LaunchDir = (ForwardDir * (1.0f - UpBias) + FVector::UpVector * UpBias).GetSafeNormal();
	if (LaunchDir.IsNearlyZero())
	{
		LaunchDir = FVector::UpVector;
	}

	const FVector LaunchVelocity = LaunchDir * DefAirKick->NPCLaunchSpeed;

	UE_LOG(LogTemp, Warning, TEXT("[AIR_MAIL_DEBUG] %s air-launched max-charge NPC %s — speed=%.0f, upBias=%.2f"),
		*Character->GetName(), *NPC->GetName(), DefAirKick->NPCLaunchSpeed, UpBias);

	NPC->LaunchIntoAir(LaunchVelocity);

	// Reuse the kick's cosmetic feedback at the contact point.
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
