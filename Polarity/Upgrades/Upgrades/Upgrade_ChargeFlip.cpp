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
#include "EMFPhysicsProp.h"

namespace
{
	bool DidWeaponDetonateProp(AEMFPhysicsProp* Prop, bool bKilled)
	{
		return Prop && bKilled && Prop->bCanExplode && Prop->IsDead();
	}
}

void UUpgrade_ChargeFlip::OnUpgradeActivated()
{
	DefCF = Cast<UUpgradeDefinition_ChargeFlip>(UpgradeDefinition);
	TriggeredPropFlips.Reset();
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
		UE_LOG(LogTemp, Warning, TEXT("[CHARGE_FLIP_DBG] OnWeaponFired rejected weapon=%s hitscan=%d laser=%d"),
			*GetNameSafe(Weapon),
			Weapon->IsHitscan() ? 1 : 0,
			Weapon->IsA<AShooterWeapon_Laser>() ? 1 : 0);
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
		UE_LOG(LogTemp, Warning, TEXT("[CHARGE_FLIP_DBG] OnWeaponFired trace missed start=%s end=%s"),
			*ViewLocation.ToCompactString(),
			*TraceEnd.ToCompactString());
		return;
	}

	AActor* HitActor = HitResult.GetActor();
	UE_LOG(LogTemp, Warning, TEXT("[CHARGE_FLIP_DBG] OnWeaponFired trace hit actor=%s class=%s component=%s dist=%.0f impact=%s"),
		*GetNameSafe(HitActor),
		HitActor ? *GetNameSafe(HitActor->GetClass()) : TEXT("None"),
		*GetNameSafe(HitResult.GetComponent()),
		HitResult.Distance,
		*HitResult.ImpactPoint.ToCompactString());

	// Check if we hit an EMF projectile
	AEMFProjectile* HitProjectile = Cast<AEMFProjectile>(HitActor);
	if (!HitProjectile)
	{
		if (AEMFPhysicsProp* Prop = Cast<AEMFPhysicsProp>(HitActor))
		{
			UE_LOG(LogTemp, Warning, TEXT("[CHARGE_FLIP_DBG] OnWeaponFired trace hit prop=%s canExplode=%d isDead=%d reverseFlight=%d charge=%.1f"),
				*GetNameSafe(Prop),
				Prop->bCanExplode ? 1 : 0,
				Prop->IsDead() ? 1 : 0,
				Prop->IsInReverseFlight() ? 1 : 0,
				Prop->GetCharge());

			const bool bAlreadyTriggered = TriggeredPropFlips.Contains(TObjectKey<AEMFPhysicsProp>(Prop));
			if (Prop->bCanExplode && Prop->IsInReverseFlight() && !Prop->IsDead() && !bAlreadyTriggered)
			{
				AController* Controller = Character->GetController();
				if (!Controller)
				{
					UE_LOG(LogTemp, Warning, TEXT("[CHARGE_FLIP_DBG] OnWeaponFired prop rejected: no controller prop=%s"),
						*GetNameSafe(Prop));
					return;
				}

				FDamageEvent DamageEvent;
				DamageEvent.DamageTypeClass = UDamageType_Ranged::StaticClass();
				const float RequestedDamage = Weapon->GetHitscanDamage();
				const float ActualDamage = Prop->TakeDamage(RequestedDamage, DamageEvent, Controller, Weapon);
				const bool bDetonated = Prop->IsDead();

				UE_LOG(LogTemp, Warning, TEXT("[CHARGE_FLIP_DBG] OnWeaponFired prop shot requested=%.1f actual=%.1f detonated=%d prop=%s"),
					RequestedDamage,
					ActualDamage,
					bDetonated ? 1 : 0,
					*GetNameSafe(Prop));

				if (bDetonated)
				{
					TSet<AEMFProjectile*> AlreadyDetonated;
					TriggeredPropFlips.Add(TObjectKey<AEMFPhysicsProp>(Prop));
					TriggerChargeFlipAtLocation(Prop->GetActorLocation(), Prop, 0, AlreadyDetonated);
				}
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("[CHARGE_FLIP_DBG] OnWeaponFired prop rejected canExplode=%d reverseFlight=%d isDead=%d alreadyTriggered=%d prop=%s"),
					Prop->bCanExplode ? 1 : 0,
					Prop->IsInReverseFlight() ? 1 : 0,
					Prop->IsDead() ? 1 : 0,
					bAlreadyTriggered ? 1 : 0,
					*GetNameSafe(Prop));
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[CHARGE_FLIP_DBG] OnWeaponFired hit non-projectile actor=%s"),
				*GetNameSafe(HitActor));
		}
		return;
	}

	// Trigger the chain!
	TSet<AEMFProjectile*> AlreadyDetonated;
	UE_LOG(LogTemp, Warning, TEXT("[CHARGE_FLIP_DBG] OnWeaponFired accepted projectile=%s"),
		*GetNameSafe(HitProjectile));
	TriggerChargeFlip(HitProjectile, 0, AlreadyDetonated);
}

void UUpgrade_ChargeFlip::OnWeaponDealtDamage(AShooterWeapon* Weapon, AActor* Target, float Damage, bool bKilled)
{
	if (!DefCF.IsValid() || !Weapon || !Target)
	{
		UE_LOG(LogTemp, Warning, TEXT("[CHARGE_FLIP_DBG] OnWeaponDealtDamage early reject def=%d weapon=%s target=%s damage=%.1f"),
			DefCF.IsValid() ? 1 : 0,
			*GetNameSafe(Weapon),
			*GetNameSafe(Target),
			Damage);
		return;
	}

	AEMFPhysicsProp* PropTarget = Cast<AEMFPhysicsProp>(Target);
	UE_LOG(LogTemp, Warning, TEXT("[CHARGE_FLIP_DBG] OnWeaponDealtDamage weapon=%s hitscan=%d laser=%d target=%s class=%s damage=%.1f killed=%d prop=%d"),
		*GetNameSafe(Weapon),
		Weapon->IsHitscan() ? 1 : 0,
		Weapon->IsA<AShooterWeapon_Laser>() ? 1 : 0,
		*GetNameSafe(Target),
		*GetNameSafe(Target->GetClass()),
		Damage,
		bKilled ? 1 : 0,
		PropTarget ? 1 : 0);

	if (PropTarget)
	{
		UE_LOG(LogTemp, Warning, TEXT("[CHARGE_FLIP_DBG] Prop state after damage prop=%s canExplode=%d isDead=%d reverseFlight=%d charge=%.1f"),
			*GetNameSafe(PropTarget),
			PropTarget->bCanExplode ? 1 : 0,
			PropTarget->IsDead() ? 1 : 0,
			PropTarget->IsInReverseFlight() ? 1 : 0,
			PropTarget->GetCharge());
	}

	const bool bAlreadyTriggered = PropTarget && TriggeredPropFlips.Contains(TObjectKey<AEMFPhysicsProp>(PropTarget));
	const bool bDidDetonateProp = DidWeaponDetonateProp(PropTarget, bKilled);
	if (!Weapon->IsHitscan() || Weapon->IsA<AShooterWeapon_Laser>() || !bDidDetonateProp || bAlreadyTriggered)
	{
		UE_LOG(LogTemp, Warning, TEXT("[CHARGE_FLIP_DBG] OnWeaponDealtDamage rejected target=%s didDetonateProp=%d alreadyTriggered=%d"),
			*GetNameSafe(Target),
			bDidDetonateProp ? 1 : 0,
			bAlreadyTriggered ? 1 : 0);
		return;
	}

	TSet<AEMFProjectile*> AlreadyDetonated;
	TriggeredPropFlips.Add(TObjectKey<AEMFPhysicsProp>(PropTarget));
	UE_LOG(LogTemp, Warning, TEXT("[CHARGE_FLIP_DBG] OnWeaponDealtDamage accepted exploded prop=%s origin=%s damage=%.1f killed=%d"),
		*GetNameSafe(Target),
		*Target->GetActorLocation().ToCompactString(),
		Damage,
		bKilled ? 1 : 0);
	TriggerChargeFlipAtLocation(Target->GetActorLocation(), Target, 0, AlreadyDetonated);
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

	const FVector ExplosionOrigin = Projectile->GetActorLocation();
	UE_LOG(LogTemp, Warning, TEXT("[CHARGE_FLIP_DBG] TriggerChargeFlip projectile=%s chainDepth=%d origin=%s already=%d"),
		*GetNameSafe(Projectile),
		ChainDepth,
		*ExplosionOrigin.ToCompactString(),
		AlreadyDetonated.Num());
	Projectile->Destroy();

	TriggerChargeFlipAtLocation(ExplosionOrigin, Projectile, ChainDepth, AlreadyDetonated);
}

void UUpgrade_ChargeFlip::TriggerChargeFlipAtLocation(const FVector& ExplosionOrigin, AActor* SourceActor, int32 ChainDepth, TSet<AEMFProjectile*>& AlreadyDetonated)
{
	if (!DefCF.IsValid())
	{
		return;
	}

	// Check chain depth limit
	if (DefCF->MaxChainDepth >= 0 && ChainDepth > DefCF->MaxChainDepth)
	{
		return;
	}

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
	if (SourceActor)
	{
		LOSParams.AddIgnoredActor(SourceActor);
	}

	// Collect EMF projectiles that need chain detonation (defer to avoid iterator invalidation)
	TArray<AEMFProjectile*> ProjectilesToChain;
	int32 VisiblePawnCount = 0;
	int32 DamagedPawnCount = 0;
	int32 BlockedPawnCount = 0;
	int32 ChainProjectileCount = 0;
	int32 BlockedProjectileCount = 0;

	UE_LOG(LogTemp, Warning, TEXT("[CHARGE_FLIP_DBG] Burst begin origin=%s source=%s chainDepth=%d damage=%.0f weapon=%s alreadyProjectiles=%d"),
		*ExplosionOrigin.ToCompactString(),
		*GetNameSafe(SourceActor),
		ChainDepth,
		FlipDamage,
		*GetNameSafe(Weapon),
		AlreadyDetonated.Num());

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
			BlockedPawnCount++;
			UE_LOG(LogTemp, Warning, TEXT("[CHARGE_FLIP_DBG] Pawn blocked target=%s blocker=%s"),
				*GetNameSafe(TargetPawn),
				*GetNameSafe(LOSHit.GetActor()));
			continue; // Blocked by a wall
		}

		VisiblePawnCount++;

		// Apply damage
		FDamageEvent DamageEvent;
		DamageEvent.DamageTypeClass = UDamageType_Ranged::StaticClass();
		float ActualDamage = TargetPawn->TakeDamage(FlipDamage, DamageEvent, Controller, Weapon);
		DamagedPawnCount++;

		// Apply ionization
		ApplyIonization(TargetPawn);

		// Spawn beam VFX
		SpawnBeamEffect(ExplosionOrigin, TargetLocation);

		UE_LOG(LogTemp, Log, TEXT("  Charge Flip hit: %s (damage %.0f)"), *TargetPawn->GetName(), ActualDamage);
		UE_LOG(LogTemp, Warning, TEXT("[CHARGE_FLIP_DBG] Pawn hit target=%s requested=%.0f actual=%.0f"),
			*GetNameSafe(TargetPawn),
			FlipDamage,
			ActualDamage);
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
			BlockedProjectileCount++;
			UE_LOG(LogTemp, Warning, TEXT("[CHARGE_FLIP_DBG] Projectile chain blocked projectile=%s blocker=%s"),
				*GetNameSafe(OtherProjectile),
				*GetNameSafe(LOSHit.GetActor()));
			continue; // Blocked by a wall
		}

		// Spawn beam to this projectile
		SpawnBeamEffect(ExplosionOrigin, ProjLocation);

		// Defer the chain detonation (don't recurse during iteration)
		ProjectilesToChain.Add(OtherProjectile);
		ChainProjectileCount++;
		UE_LOG(LogTemp, Warning, TEXT("[CHARGE_FLIP_DBG] Projectile queued for chain projectile=%s chainDepth=%d"),
			*GetNameSafe(OtherProjectile),
			ChainDepth + 1);
	}

	UE_LOG(LogTemp, Warning, TEXT("[CHARGE_FLIP_DBG] Burst summary origin=%s visiblePawns=%d damagedPawns=%d blockedPawns=%d chainProjectiles=%d blockedProjectiles=%d"),
		*ExplosionOrigin.ToCompactString(),
		VisiblePawnCount,
		DamagedPawnCount,
		BlockedPawnCount,
		ChainProjectileCount,
		BlockedProjectileCount);

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
		const float NewCharge = CurrentCharge + DefCF->IonizationChargePerHit;
		TargetModifier->SetCharge(NewCharge);
		return;
	}

	// Route through SetCharge() for props (enables physics on first charge)
	if (AEMFPhysicsProp* Prop = Cast<AEMFPhysicsProp>(Target))
	{
		const float CurrentCharge = Prop->GetCharge();
		Prop->SetCharge(CurrentCharge + DefCF->IonizationChargePerHit);
		return;
	}

	// Generic fallback: UEMF_FieldComponent
	if (UEMF_FieldComponent* TargetField = Target->FindComponentByClass<UEMF_FieldComponent>())
	{
		FEMSourceDescription Desc = TargetField->GetSourceDescription();
		const float CurrentCharge = Desc.PointChargeParams.Charge;
		Desc.PointChargeParams.Charge = CurrentCharge + DefCF->IonizationChargePerHit;
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
