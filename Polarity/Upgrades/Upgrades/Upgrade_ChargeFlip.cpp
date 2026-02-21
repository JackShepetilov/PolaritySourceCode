// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "Upgrade_ChargeFlip.h"
#include "UpgradeDefinition_ChargeFlip.h"
#include "ShooterCharacter.h"
#include "ShooterWeapon.h"
#include "ShooterWeapon_Laser.h"
#include "EMFProjectile.h"
#include "EMFVelocityModifier.h"
#include "EMF_FieldComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "Engine/DamageEvents.h"
#include "EngineUtils.h"
#include "Variant_Shooter/DamageTypes/DamageType_Ranged.h"

void UUpgrade_ChargeFlip::OnUpgradeActivated()
{
	DefCF = Cast<UUpgradeDefinition_ChargeFlip>(UpgradeDefinition);
	if (!DefCF.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("Charge Flip: UpgradeDefinition is not UUpgradeDefinition_ChargeFlip!"));
	}
}

void UUpgrade_ChargeFlip::OnWeaponFired()
{
	if (!DefCF.IsValid())
	{
		return;
	}

	AShooterCharacter* Character = GetShooterCharacter();
	AShooterWeapon* Weapon = GetCurrentWeapon();
	if (!Character || !Weapon)
	{
		return;
	}

	// Only hitscan weapons, NOT lasers
	if (!Weapon->IsHitscan() || Weapon->IsA<AShooterWeapon_Laser>())
	{
		return;
	}

	// Trace from camera viewpoint (same as weapon's hitscan)
	FVector ViewLocation = Character->GetPawnViewLocation();
	FVector ViewDirection = Character->GetBaseAimRotation().Vector();
	FVector TraceEnd = ViewLocation + ViewDirection * 20000.0f;

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(Character);
	QueryParams.AddIgnoredActor(Weapon);
	QueryParams.bReturnPhysicalMaterial = false;

	// Trace by ECC_Visibility — same as weapon's step 1. Projectiles block all channels.
	FHitResult HitResult;
	bool bHit = GetWorld()->LineTraceSingleByChannel(
		HitResult, ViewLocation, TraceEnd, ECC_Visibility, QueryParams);

	if (!bHit || !HitResult.GetActor())
	{
		return;
	}

	// Check if we hit an EMF projectile
	AEMFProjectile* HitProjectile = Cast<AEMFProjectile>(HitResult.GetActor());
	if (!HitProjectile)
	{
		return;
	}

	// Trigger the chain!
	TSet<AEMFProjectile*> AlreadyDetonated;
	TriggerChargeFlip(HitProjectile, 0, AlreadyDetonated);
}

void UUpgrade_ChargeFlip::TriggerChargeFlip(AEMFProjectile* Projectile, int32 ChainDepth, TSet<AEMFProjectile*>& AlreadyDetonated)
{
	if (!Projectile || !DefCF.IsValid())
	{
		return;
	}

	// Prevent infinite loops
	if (AlreadyDetonated.Contains(Projectile))
	{
		return;
	}

	// Check chain depth limit
	if (DefCF->MaxChainDepth >= 0 && ChainDepth > DefCF->MaxChainDepth)
	{
		return;
	}

	AlreadyDetonated.Add(Projectile);

	AShooterCharacter* Character = GetShooterCharacter();
	AShooterWeapon* Weapon = GetCurrentWeapon();
	if (!Character || !Weapon)
	{
		return;
	}

	AController* Controller = Character->GetController();
	if (!Controller)
	{
		return;
	}

	// Store explosion origin before destroying the projectile
	FVector ExplosionOrigin = Projectile->GetActorLocation();

	// Destroy the projectile (no normal projectile damage — user specified "Only Charge Flip")
	Projectile->Destroy();

	// --- VFX/SFX at explosion point ---
	if (DefCF->ExplosionFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			GetWorld(), DefCF->ExplosionFX, ExplosionOrigin,
			FRotator::ZeroRotator, FVector::OneVector, true, true, ENCPoolMethod::None);
	}

	if (DefCF->ExplosionSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, DefCF->ExplosionSound, ExplosionOrigin);
	}

	// Calculate damage: weapon's HitscanDamage * multiplier
	float FlipDamage = Weapon->GetHitscanDamage() * DefCF->DamageMultiplier;

	// Collision params for LOS checks
	FCollisionQueryParams LOSParams;
	LOSParams.AddIgnoredActor(Character);
	LOSParams.AddIgnoredActor(Weapon);

	// Collect EMF projectiles that need chain detonation (defer to avoid iterator invalidation)
	TArray<AEMFProjectile*> ProjectilesToChain;

	UE_LOG(LogTemp, Log, TEXT("Charge Flip: Explosion at %s (chain depth %d, damage %.0f)"),
		*ExplosionOrigin.ToString(), ChainDepth, FlipDamage);

	// --- Hit all visible PAWNS from explosion point ---
	for (TActorIterator<APawn> It(GetWorld()); It; ++It)
	{
		APawn* TargetPawn = *It;

		// Skip the player
		if (TargetPawn == Character)
		{
			continue;
		}

		// Skip dead/pending-kill
		if (!IsValid(TargetPawn))
		{
			continue;
		}

		FVector TargetLocation = TargetPawn->GetActorLocation();

		// LOS check from explosion origin to target
		FHitResult LOSHit;
		bool bBlocked = GetWorld()->LineTraceSingleByChannel(
			LOSHit, ExplosionOrigin, TargetLocation, ECC_Visibility, LOSParams);

		// If the trace hit something, check if it's the target itself (= visible)
		if (bBlocked && LOSHit.GetActor() != TargetPawn)
		{
			continue; // Blocked by a wall
		}

		// Apply damage
		FDamageEvent DamageEvent;
		DamageEvent.DamageTypeClass = UDamageType_Ranged::StaticClass();
		float ActualDamage = TargetPawn->TakeDamage(FlipDamage, DamageEvent, Controller, Weapon);

		// Apply ionization
		ApplyIonization(TargetPawn);

		// Spawn beam VFX
		SpawnBeamEffect(ExplosionOrigin, TargetLocation);

		UE_LOG(LogTemp, Log, TEXT("  Charge Flip hit: %s (damage %.0f)"), *TargetPawn->GetName(), ActualDamage);
	}

	// --- Hit all visible EMF PROJECTILES for chain reaction ---
	for (TActorIterator<AEMFProjectile> It(GetWorld()); It; ++It)
	{
		AEMFProjectile* OtherProjectile = *It;

		// Skip already detonated and pending-kill
		if (!IsValid(OtherProjectile) || AlreadyDetonated.Contains(OtherProjectile))
		{
			continue;
		}

		FVector ProjLocation = OtherProjectile->GetActorLocation();

		// LOS check
		FHitResult LOSHit;
		bool bBlocked = GetWorld()->LineTraceSingleByChannel(
			LOSHit, ExplosionOrigin, ProjLocation, ECC_Visibility, LOSParams);

		if (bBlocked && LOSHit.GetActor() != OtherProjectile)
		{
			continue; // Blocked by a wall
		}

		// Spawn beam to this projectile
		SpawnBeamEffect(ExplosionOrigin, ProjLocation);

		// Defer the chain detonation (don't recurse during iteration)
		ProjectilesToChain.Add(OtherProjectile);
	}

	// Chain detonate collected projectiles
	for (AEMFProjectile* ChainProj : ProjectilesToChain)
	{
		if (IsValid(ChainProj))
		{
			TriggerChargeFlip(ChainProj, ChainDepth + 1, AlreadyDetonated);
		}
	}
}

void UUpgrade_ChargeFlip::ApplyIonization(AActor* Target)
{
	if (!Target || !DefCF.IsValid())
	{
		return;
	}

	// Try UEMFVelocityModifier first (for characters/NPCs)
	if (UEMFVelocityModifier* TargetModifier = Target->FindComponentByClass<UEMFVelocityModifier>())
	{
		const float CurrentCharge = TargetModifier->GetCharge();
		if (CurrentCharge >= DefCF->MaxIonizationCharge)
		{
			return;
		}
		const float NewCharge = FMath::Min(CurrentCharge + DefCF->IonizationChargePerHit, DefCF->MaxIonizationCharge);
		TargetModifier->SetCharge(NewCharge);
		return;
	}

	// Fallback: UEMF_FieldComponent (for physics props)
	if (UEMF_FieldComponent* TargetField = Target->FindComponentByClass<UEMF_FieldComponent>())
	{
		FEMSourceDescription Desc = TargetField->GetSourceDescription();
		const float CurrentCharge = Desc.PointChargeParams.Charge;
		if (CurrentCharge >= DefCF->MaxIonizationCharge)
		{
			return;
		}
		Desc.PointChargeParams.Charge = FMath::Min(CurrentCharge + DefCF->IonizationChargePerHit, DefCF->MaxIonizationCharge);
		TargetField->SetSourceDescription(Desc);
	}
}

void UUpgrade_ChargeFlip::SpawnBeamEffect(const FVector& Start, const FVector& End)
{
	if (!DefCF.IsValid() || !DefCF->BeamFX)
	{
		return;
	}

	UNiagaraComponent* BeamComp = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		GetWorld(),
		DefCF->BeamFX,
		Start,
		(End - Start).Rotation(),
		FVector::OneVector,
		true,
		true,
		ENCPoolMethod::None
	);

	if (BeamComp)
	{
		BeamComp->SetVectorParameter(FName("BeamStart"), Start);
		BeamComp->SetVectorParameter(FName("BeamEnd"), End);
		BeamComp->SetFloatParameter(FName("Energy"), 1.0f);
		BeamComp->SetColorParameter(FName("BeamColor"), DefCF->BeamColor);
	}
}
