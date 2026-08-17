// AbilityHandler_TankPassive.cpp

#include "AbilityHandler_TankPassive.h"
#include "AbilityDefinition_TankPassive.h"
#include "AbilityComponent.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"
#include "Variant_Shooter/Pickups/HealthPickup.h"
#include "AI/Coordination/ThreatComponent.h"
#include "EMFVelocityModifier.h"
#include "Kismet/GameplayStatics.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"

void UAbilityHandler_TankPassive::OnEquip_Implementation()
{
	AShooterCharacter* Character = GetOwningCharacter();
	if (!Character || !Character->HasAuthority())
	{
		// Clients build a handler too, because the passive channel mirrors the way slots work. It
		// just has nothing to do: every effect below is a decision about the world.
		return;
	}

	DeathDelegateHandle = AShooterNPC::OnAnyNPCDeath.AddUObject(this, &UAbilityHandler_TankPassive::HandleAnyNPCDeath);
	RebindWeapon();

	// Bring the component the provocation writes into, if nobody else has.
	ResolveThreatComponent();

	UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] TankPassive equipped on %s"), *Character->GetName());
}

void UAbilityHandler_TankPassive::OnUnequip_Implementation()
{
	if (DeathDelegateHandle.IsValid())
	{
		AShooterNPC::OnAnyNPCDeath.Remove(DeathDelegateHandle);
		DeathDelegateHandle.Reset();
	}

	if (AShooterWeapon* Weapon = BoundWeapon.Get())
	{
		Weapon->OnShotFired.RemoveDynamic(this, &UAbilityHandler_TankPassive::HandleShotFired);
	}
	BoundWeapon.Reset();
}

void UAbilityHandler_TankPassive::OnPassiveTick(float DeltaTime)
{
	AShooterCharacter* Character = GetOwningCharacter();
	if (!Character || !Character->HasAuthority())
	{
		return;
	}

	// Two pointers compared per frame. The alternative is a notification from the character, and
	// the character announces a weapon change from five different places -- adding a sixth listener
	// to each of them is more code and more ways to miss one than this is cost.
	if (BoundWeapon.Get() != Character->GetCurrentWeapon())
	{
		RebindWeapon();
	}
}

void UAbilityHandler_TankPassive::RebindWeapon()
{
	AShooterCharacter* Character = GetOwningCharacter();

	if (AShooterWeapon* Old = BoundWeapon.Get())
	{
		Old->OnShotFired.RemoveDynamic(this, &UAbilityHandler_TankPassive::HandleShotFired);
	}
	BoundWeapon.Reset();

	AShooterWeapon* Weapon = Character ? Character->GetCurrentWeapon() : nullptr;
	if (!Weapon)
	{
		return;
	}

	Weapon->OnShotFired.AddDynamic(this, &UAbilityHandler_TankPassive::HandleShotFired);
	BoundWeapon = Weapon;
}

UThreatComponent* UAbilityHandler_TankPassive::ResolveThreatComponent() const
{
	AShooterCharacter* Character = GetOwningCharacter();
	if (!Character)
	{
		return nullptr;
	}

	if (UThreatComponent* Existing = Character->FindComponentByClass<UThreatComponent>())
	{
		return Existing;
	}

	// Server side only, which is where threat is read: see UThreatComponent's own note. Creating it
	// here rather than requiring it on the class Blueprint means the passive cannot be installed
	// half-working, and it costs one component on one character.
	UThreatComponent* Created = NewObject<UThreatComponent>(Character);
	if (Created)
	{
		Created->RegisterComponent();
	}
	return Created;
}

void UAbilityHandler_TankPassive::HandleShotFired()
{
	const UAbilityDefinition_TankPassive* Def = Cast<UAbilityDefinition_TankPassive>(GetDefinition());
	AShooterCharacter* Character = GetOwningCharacter();
	if (!Def || !Character || !Character->HasAuthority() || Character->IsDead())
	{
		return;
	}

	const FTankPassiveLevelStats Stats = Def->GetStatsAtLevel(GetCurrentLevel());
	if (Stats.ThreatPerShot <= 0.0f)
	{
		return;
	}

	if (UThreatComponent* Threat = ResolveThreatComponent())
	{
		// No radius here on purpose. Threat is weighed against distance by the coordinator, so a shot
		// fired across the map already counts for almost nothing where it does not matter, and a
		// radius on top would be a second falloff doing the same job worse.
		Threat->AddThreat(Stats.ThreatPerShot, Stats.ThreatDecaySeconds);
	}
}

void UAbilityHandler_TankPassive::HandleAnyNPCDeath(AShooterNPC* DeadNPC, TSubclassOf<UDamageType> KillingDamageType, AActor* KillingCauser)
{
	const UAbilityDefinition_TankPassive* Def = Cast<UAbilityDefinition_TankPassive>(GetDefinition());
	AShooterCharacter* Character = GetOwningCharacter();
	UWorld* World = Character ? Character->GetWorld() : nullptr;

	if (!Def || !DeadNPC || !World || !Character->HasAuthority() || Character->IsDead())
	{
		return;
	}

	const FTankPassiveLevelStats Stats = Def->GetStatsAtLevel(GetCurrentLevel());
	if (Stats.KillDropCount <= 0 || Stats.KillDropRadius <= 0.0f)
	{
		return;
	}

	const float DistSq = FVector::DistSquared(DeadNPC->GetActorLocation(), Character->GetActorLocation());
	if (DistSq > FMath::Square(Stats.KillDropRadius))
	{
		return;
	}

	// The enemy's own pickup class unless the passive overrides it, so one kind of health pickup
	// stays one kind of health pickup.
	const TSubclassOf<AHealthPickup> PickupClass =
		Def->OverrideHealthPickupClass ? Def->OverrideHealthPickupClass : DeadNPC->HealthPickupClass;
	if (!PickupClass)
	{
		return;
	}

	AHealthPickup::SpawnHealthPickups(World, PickupClass, DeadNPC->GetActorLocation(),
		Stats.KillDropCount, DeadNPC->HealthPickupScatterRadius, DeadNPC->HealthPickupFloorOffset);

	UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] TankPassive: %s died %.0f cm from %s, dropping %d extra health"),
		*DeadNPC->GetName(), FMath::Sqrt(DistSq), *Character->GetName(), Stats.KillDropCount);
}

FVector UAbilityHandler_TankPassive::ResolveMirroredPoint(const AShooterNPC* Enemy, FName HitBone) const
{
	if (!Enemy)
	{
		return FVector::ZeroVector;
	}

	if (HitBone != NAME_None)
	{
		if (const USkeletalMeshComponent* Mesh = Enemy->GetMesh())
		{
			// Asking the mesh whether it has the bone is the whole "did the skeletons match" test, and
			// it is the right test: two enemies can share a skeleton asset or not, but what matters is
			// only whether this name resolves to somewhere on this body.
			if (Mesh->GetBoneIndex(HitBone) != INDEX_NONE)
			{
				const FVector BoneLocation = Mesh->GetBoneLocation(HitBone);
				if (UAbilityComponent::IsBeamDebugEnabled())
				{
					UE_LOG(LogTemp, Warning, TEXT("[BEAM_DEBUG]   end: bone '%s' matched on %s -> %s"),
						*HitBone.ToString(), *Enemy->GetName(), *BoneLocation.ToCompactString());
				}
				return BoneLocation;
			}

			if (UAbilityComponent::IsBeamDebugEnabled())
			{
				UE_LOG(LogTemp, Warning, TEXT("[BEAM_DEBUG]   end: bone '%s' NOT on %s (other skeleton) -> centre"),
					*HitBone.ToString(), *Enemy->GetName());
			}
		}
	}
	else if (UAbilityComponent::IsBeamDebugEnabled())
	{
		UE_LOG(LogTemp, Warning, TEXT("[BEAM_DEBUG]   end: damage event carried no bone -> centre of %s"),
			*Enemy->GetName());
	}

	return Enemy->GetActorLocation();
}

void UAbilityHandler_TankPassive::OnOwnerDamaged(float Damage, AActor* DamageCauser, AController* InstigatedBy, const FHitResult& HitInfo)
{
	const UAbilityDefinition_TankPassive* Def = Cast<UAbilityDefinition_TankPassive>(GetDefinition());
	AShooterCharacter* Character = GetOwningCharacter();
	// A killing blow returns nothing: this is called after health has already moved, so IsDead here
	// means the hit being answered is the one that killed him.
	if (!Def || !Character || !Character->HasAuthority() || Damage <= 0.0f || Character->IsDead())
	{
		return;
	}

	// Who actually shot, as opposed to what arrived. A projectile is the causer, its shooter is the
	// instigator, and a melee hit is both at once -- all three roads are tried before giving up.
	AShooterNPC* Attacker = Cast<AShooterNPC>(DamageCauser);
	if (!Attacker && InstigatedBy)
	{
		Attacker = Cast<AShooterNPC>(InstigatedBy->GetPawn());
	}
	if (!Attacker && DamageCauser)
	{
		Attacker = Cast<AShooterNPC>(DamageCauser->GetInstigator());
	}
	if (!Attacker || Attacker->IsDead())
	{
		return;
	}

	// Shield is the charge magnitude, exactly as the Tank's active reads it: what the team has
	// already stripped off this enemy is what makes hitting the Tank expensive. An untouched enemy
	// pays nothing, which is what stops this from being a flat damage aura.
	const UEMFVelocityModifier* Modifier = Attacker->FindComponentByClass<UEMFVelocityModifier>();
	if (!Modifier)
	{
		return;
	}

	// Normalised against this enemy's own cap rather than a number kept here. MaxBaseCharge is where
	// the shield reads empty (IsAtMaxCharge), it is authored per enemy, and a constant in the ability
	// would quietly disagree with every enemy tuned away from whatever it was set to.
	const float MaxStrippable = Modifier->MaxBaseCharge;
	if (MaxStrippable <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	const float Stripped = FMath::Abs(Modifier->GetCharge());
	const FTankPassiveLevelStats Stats = Def->GetStatsAtLevel(GetCurrentLevel());
	const float Scale = FMath::Clamp(Stripped / MaxStrippable, 0.0f, 1.0f);

	const float Reflected = FMath::Min(Damage * Stats.ReflectFractionAtFullStrip * Scale, Stats.MaxReflectPerHit);
	if (Reflected <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	// Credited to the Tank, because it is his ability doing it: kill credit, threat and everything
	// else downstream of damage should read this as the Tank hurting the enemy, not as the enemy
	// hurting itself.
	UGameplayStatics::ApplyDamage(Attacker, Reflected, Character->GetController(), Character, nullptr);

	// Drawn from the wound outwards, so the beam and its sound explain themselves: they leave where
	// the Tank was hit and arrive where that hit is being paid back. The start comes from the damage
	// event rather than from the Tank's centre, which is why HitInfo is carried all the way down here.
	if (Stats.ReflectVFX || Stats.ReflectSound)
	{
		if (UAbilityComponent* Component = GetOwningComponent())
		{
			// ImpactPoint is an FVector_NetQuantize, not an FVector -- explicit on both arms so the
			// ternary isn't left guessing which way to convert.
			const FVector ImpactPoint(HitInfo.ImpactPoint);
			const bool bHadImpactPoint = !ImpactPoint.IsNearlyZero();
			const FVector Start = bHadImpactPoint ? ImpactPoint : Character->GetActorLocation();
			const FVector End = ResolveMirroredPoint(Attacker, HitInfo.BoneName);

			if (UAbilityComponent::IsBeamDebugEnabled())
			{
				// The discriminating measurement. If DistToTank is large and DistToEnemy is small, the
				// damage event handed us a point on the ENEMY (a radial event reports its origin, which
				// for an exploding drone is the drone), and the beam is inside the enemy through no
				// fault of the drawing code.
				UE_LOG(LogTemp, Warning,
					TEXT("[BEAM_DEBUG] REFLECT causer=%s instigatorPawn=%s hadImpactPoint=%d")
					TEXT(" start=%s startDistToTank=%.0f startDistToEnemy=%.0f"),
					*GetNameSafe(DamageCauser),
					*GetNameSafe(InstigatedBy ? InstigatedBy->GetPawn() : nullptr),
					bHadImpactPoint ? 1 : 0,
					*Start.ToCompactString(),
					FVector::Dist(Start, Character->GetActorLocation()),
					FVector::Dist(Start, Attacker->GetActorLocation()));
			}

			Component->Multicast_PlayBeamVFX(Stats.ReflectVFX, Stats.ReflectSound, Start, End,
				Stats.ReflectVFXScale);
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] TankPassive: returned %.1f of %.1f to %s (stripped %.1f of %.1f shield, scale %.2f, bone '%s')"),
		Reflected, Damage, *Attacker->GetName(), Stripped, MaxStrippable, Scale, *HitInfo.BoneName.ToString());
}
