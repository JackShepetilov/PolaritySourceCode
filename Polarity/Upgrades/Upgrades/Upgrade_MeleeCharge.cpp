// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "Upgrade_MeleeCharge.h"
#include "UpgradeDefinition_MeleeCharge.h"
#include "ApexMovementComponent.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"
#include "Variant_Shooter/Weapons/ShooterWeapon_Melee.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "Variant_Shooter/ShooterDummy.h"
#include "DrawDebugHelpers.h"
#include "Engine/DamageEvents.h"
#include "GameFramework/Controller.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Upgrades/UpgradeManagerComponent.h"

UUpgrade_MeleeCharge::UUpgrade_MeleeCharge()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UUpgrade_MeleeCharge::OnUpgradeActivated()
{
	CachedDef = Cast<UUpgradeDefinition_MeleeCharge>(UpgradeDefinition);
	if (!CachedDef.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[MELEE_CHARGE] Activation failed: definition is not UUpgradeDefinition_MeleeCharge"));
		return;
	}

	BindMovement();

	if (AShooterCharacter* Character = GetShooterCharacter())
	{
		if (AShooterWeapon_Melee* MeleeWeapon = Cast<AShooterWeapon_Melee>(Character->GetCurrentWeapon()))
		{
			BindToMeleeWeapon(MeleeWeapon);
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("[MELEE_CHARGE] Activated Lv%d/%d"),
		CurrentLevel, CachedDef->MaxLevel);
}

void UUpgrade_MeleeCharge::OnUpgradeDeactivated()
{
	EndCharge(true);
	RestoreMovementOverrides();
	UnbindMovement();

	if (AShooterWeapon_Melee* Weapon = BoundMeleeWeapon.Get())
	{
		UnbindFromMeleeWeapon(Weapon);
	}
	BoundMeleeWeapon.Reset();

	bPendingMeleeBoost = false;
	MeleeBoostTimeRemaining = 0.0f;
	SetComponentTickEnabled(false);

	UE_LOG(LogTemp, Warning, TEXT("[MELEE_CHARGE] Deactivated"));
}

void UUpgrade_MeleeCharge::OnLevelChanged(int32 OldLevel, int32 NewLevel)
{
	UE_LOG(LogTemp, Warning, TEXT("[MELEE_CHARGE] Level %d -> %d"), OldLevel, NewLevel);
}

void UUpgrade_MeleeCharge::OnWeaponChanged(AShooterWeapon* OldWeapon, AShooterWeapon* NewWeapon)
{
	if (AShooterWeapon_Melee* OldMelee = Cast<AShooterWeapon_Melee>(OldWeapon))
	{
		UnbindFromMeleeWeapon(OldMelee);
	}

	if (AShooterWeapon_Melee* NewMelee = Cast<AShooterWeapon_Melee>(NewWeapon))
	{
		BindToMeleeWeapon(NewMelee);
	}
	else
	{
		BoundMeleeWeapon.Reset();
		EndCharge(true);
	}
}

bool UUpgrade_MeleeCharge::OnWeaponSecondaryAction(AShooterWeapon* Weapon)
{
	return StartCharge(Weapon);
}

void UUpgrade_MeleeCharge::OnWeaponSecondaryActionReleased(AShooterWeapon* /*Weapon*/)
{
	// TF2 shield charge is press-to-spend, not hold-to-maintain.
}

float UUpgrade_MeleeCharge::GetMeleeDamageMultiplier(AActor* /*Target*/) const
{
	if (!bPendingMeleeBoost || MeleeBoostTimeRemaining <= 0.0f)
	{
		return 1.0f;
	}

	if (!IsEligibleWeapon(GetCurrentWeapon()))
	{
		return 1.0f;
	}

	return GetCurrentLevelData().MeleeBoostMultiplier;
}

void UUpgrade_MeleeCharge::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (CooldownRemaining > 0.0f)
	{
		CooldownRemaining = FMath::Max(0.0f, CooldownRemaining - DeltaTime);
	}

	if (bPendingMeleeBoost)
	{
		MeleeBoostTimeRemaining = FMath::Max(0.0f, MeleeBoostTimeRemaining - DeltaTime);
		if (MeleeBoostTimeRemaining <= 0.0f)
		{
			ConsumeMeleeBoost();
		}
	}

	if (bIsCharging)
	{
		ChargeTimeRemaining -= DeltaTime;
		ActiveChargeDuration += DeltaTime;

		if (AShooterCharacter* Character = GetShooterCharacter())
		{
			const FVector CurrentLocation = Character->GetActorLocation();
			if (bHasPreviousChargeLocation)
			{
				SweepChargePath(PreviousChargeLocation, CurrentLocation);

				const FMeleeChargeLevelData& Data = GetCurrentLevelData();
				if (Data.bEndOnBlockedMovement && ActiveChargeDuration > 0.05f)
				{
					const float ExpectedDistance = Data.ChargeSpeed * DeltaTime;
					const float ActualDistance = FVector::Dist2D(PreviousChargeLocation, CurrentLocation);
					if (ExpectedDistance > KINDA_SMALL_NUMBER && ActualDistance < ExpectedDistance * Data.BlockedMoveFraction)
					{
						BlockedMoveTime += DeltaTime;
						if (BlockedMoveTime >= 0.05f)
						{
							EndCharge(true);
						}
					}
					else
					{
						BlockedMoveTime = 0.0f;
					}
				}
			}

			PreviousChargeLocation = CurrentLocation;
			bHasPreviousChargeLocation = true;
		}

		if (bIsCharging && ChargeTimeRemaining <= 0.0f)
		{
			EndCharge(false);
		}
	}

	if (!bIsCharging && CooldownRemaining <= 0.0f && !bPendingMeleeBoost)
	{
		SetComponentTickEnabled(false);
	}
}

void UUpgrade_MeleeCharge::BindMovement()
{
	if (AShooterCharacter* Character = GetShooterCharacter())
	{
		CachedMovement = Cast<UApexMovementComponent>(Character->GetCharacterMovement());
		if (UApexMovementComponent* Movement = CachedMovement.Get())
		{
			Movement->OnPreVelocityUpdate.RemoveAll(this);
			Movement->OnPreVelocityUpdate.AddUObject(this, &UUpgrade_MeleeCharge::HandlePreVelocityUpdate);
		}
	}
}

void UUpgrade_MeleeCharge::UnbindMovement()
{
	if (UApexMovementComponent* Movement = CachedMovement.Get())
	{
		Movement->OnPreVelocityUpdate.RemoveAll(this);
	}
	CachedMovement.Reset();
}

void UUpgrade_MeleeCharge::BindToMeleeWeapon(AShooterWeapon_Melee* Weapon)
{
	if (!Weapon || BoundMeleeWeapon.Get() == Weapon)
	{
		return;
	}

	if (AShooterWeapon_Melee* Existing = BoundMeleeWeapon.Get())
	{
		UnbindFromMeleeWeapon(Existing);
	}

	BoundMeleeWeapon = Weapon;
	Weapon->OnMeleeWeaponHit.AddDynamic(this, &UUpgrade_MeleeCharge::HandleMeleeWeaponHit);
}

void UUpgrade_MeleeCharge::UnbindFromMeleeWeapon(AShooterWeapon_Melee* Weapon)
{
	if (!Weapon)
	{
		return;
	}

	Weapon->OnMeleeWeaponHit.RemoveDynamic(this, &UUpgrade_MeleeCharge::HandleMeleeWeaponHit);
	if (BoundMeleeWeapon.Get() == Weapon)
	{
		BoundMeleeWeapon.Reset();
	}
}

bool UUpgrade_MeleeCharge::CanStartCharge(AShooterWeapon* Weapon) const
{
	if (!CachedDef.IsValid() || bIsCharging || CooldownRemaining > 0.0f || !IsEligibleWeapon(Weapon))
	{
		return false;
	}

	const AShooterCharacter* Character = GetShooterCharacter();
	if (!Character || Character->IsDead())
	{
		return false;
	}

	const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	return Movement && Movement->UpdatedComponent;
}

bool UUpgrade_MeleeCharge::IsEligibleWeapon(const AShooterWeapon* Weapon) const
{
	if (!Weapon || !Weapon->IsMeleeWeapon())
	{
		return false;
	}

	const FMeleeChargeLevelData& Data = GetCurrentLevelData();
	return !Data.RequiredWeaponClass || Weapon->IsA(Data.RequiredWeaponClass);
}

bool UUpgrade_MeleeCharge::StartCharge(AShooterWeapon* Weapon)
{
	if (!CanStartCharge(Weapon))
	{
		return false;
	}

	AShooterCharacter* Character = GetShooterCharacter();
	if (!Character)
	{
		return false;
	}

	const FMeleeChargeLevelData& Data = GetCurrentLevelData();
	ChargeDirection = GetDesiredChargeDirection();
	ChargeTimeRemaining = Data.ChargeDuration;
	ActiveChargeDuration = 0.0f;
	BlockedMoveTime = 0.0f;
	bIsCharging = true;
	bHasPreviousChargeLocation = true;
	PreviousChargeLocation = Character->GetActorLocation();
	HitActorsThisCharge.Empty();
	ApplyMovementOverrides();

	if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
	{
		FVector NewVelocity = ChargeDirection * Data.ChargeSpeed;
		NewVelocity.Z = 0.0f;
		Movement->Velocity = NewVelocity;
	}

	SetComponentTickEnabled(true);

	UE_LOG(LogTemp, Warning, TEXT("[MELEE_CHARGE] START speed=%.0f duration=%.2f cooldown=%.2f"),
		Data.ChargeSpeed, Data.ChargeDuration, Data.Cooldown);

	return true;
}

void UUpgrade_MeleeCharge::EndCharge(bool bInterrupted)
{
	if (!bIsCharging)
	{
		return;
	}

	const FMeleeChargeLevelData& Data = GetCurrentLevelData();
	bIsCharging = false;
	bHasPreviousChargeLocation = false;
	CooldownRemaining = Data.Cooldown;
	RestoreMovementOverrides();

	const float ConsumedFraction = Data.ChargeDuration > KINDA_SMALL_NUMBER
		? FMath::Clamp(ActiveChargeDuration / Data.ChargeDuration, 0.0f, 1.0f)
		: 1.0f;

	if (ConsumedFraction >= Data.MinConsumedFractionForBoost)
	{
		ArmMeleeBoost();
	}

	if (UCharacterMovementComponent* Movement = CachedMovement.Get())
	{
		Movement->Velocity *= 0.35f;
		Movement->Velocity.Z = FMath::Min(Movement->Velocity.Z, 0.0f);
	}

	UE_LOG(LogTemp, Warning, TEXT("[MELEE_CHARGE] END interrupted=%d consumed=%.2f cooldown=%.2f boost=%d"),
		bInterrupted ? 1 : 0, ConsumedFraction, CooldownRemaining, bPendingMeleeBoost ? 1 : 0);
}

void UUpgrade_MeleeCharge::ApplyMovementOverrides()
{
	UApexMovementComponent* Movement = CachedMovement.Get();
	if (!Movement || bAppliedMovementOverrides)
	{
		return;
	}

	const FMeleeChargeLevelData& Data = GetCurrentLevelData();
	bAppliedMovementOverrides = true;
	SavedGroundFriction = Movement->GroundFriction;
	SavedBrakingDecelerationWalking = Movement->BrakingDecelerationWalking;

	Movement->SetExternalMaxSpeedOverride(Data.ChargeSpeed);
	Movement->GroundFriction = 0.0f;
	Movement->BrakingDecelerationWalking = 0.0f;
}

void UUpgrade_MeleeCharge::RestoreMovementOverrides()
{
	UApexMovementComponent* Movement = CachedMovement.Get();
	if (!Movement || !bAppliedMovementOverrides)
	{
		bAppliedMovementOverrides = false;
		return;
	}

	Movement->ClearExternalMaxSpeedOverride();
	Movement->GroundFriction = SavedGroundFriction;
	Movement->BrakingDecelerationWalking = SavedBrakingDecelerationWalking;
	bAppliedMovementOverrides = false;
}

void UUpgrade_MeleeCharge::ArmMeleeBoost()
{
	const FMeleeChargeLevelData& Data = GetCurrentLevelData();
	if (Data.MeleeBoostMultiplier <= 1.0f || Data.MeleeBoostWindow <= 0.0f)
	{
		return;
	}

	bPendingMeleeBoost = true;
	MeleeBoostTimeRemaining = Data.MeleeBoostWindow;
	SetComponentTickEnabled(true);
}

void UUpgrade_MeleeCharge::ConsumeMeleeBoost()
{
	bPendingMeleeBoost = false;
	MeleeBoostTimeRemaining = 0.0f;
}

const FMeleeChargeLevelData& UUpgrade_MeleeCharge::GetCurrentLevelData() const
{
	if (const UUpgradeDefinition_MeleeCharge* Def = CachedDef.Get())
	{
		return Def->GetLevelData(CurrentLevel);
	}

	static const FMeleeChargeLevelData DefaultData;
	return DefaultData;
}

FVector UUpgrade_MeleeCharge::GetDesiredChargeDirection() const
{
	if (const AShooterCharacter* Character = GetShooterCharacter())
	{
		FVector Direction = Character->GetActorForwardVector();
		if (const AController* Controller = Character->GetController())
		{
			Direction = Controller->GetControlRotation().Vector();
		}

		Direction.Z = 0.0f;
		if (!Direction.Normalize())
		{
			Direction = Character->GetActorForwardVector();
			Direction.Z = 0.0f;
			Direction.Normalize();
		}
		return Direction;
	}

	return FVector::ForwardVector;
}

void UUpgrade_MeleeCharge::HandlePreVelocityUpdate(float DeltaTime, FVector& InOutVelocity)
{
	if (!bIsCharging)
	{
		return;
	}

	const FMeleeChargeLevelData& Data = GetCurrentLevelData();
	const FVector DesiredDirection = GetDesiredChargeDirection();
	if (Data.TurnInterpSpeed > 0.0f)
	{
		ChargeDirection = FMath::VInterpNormalRotationTo(
			ChargeDirection,
			DesiredDirection,
			DeltaTime,
			Data.TurnInterpSpeed);
	}

	ChargeDirection.Z = 0.0f;
	if (!ChargeDirection.Normalize())
	{
		ChargeDirection = DesiredDirection;
	}

	InOutVelocity = ChargeDirection * Data.ChargeSpeed;
	InOutVelocity.Z = 0.0f;
}

void UUpgrade_MeleeCharge::SweepChargePath(const FVector& Start, const FVector& End)
{
	if (Start.Equals(End, 1.0f))
	{
		return;
	}

	AShooterCharacter* Character = GetShooterCharacter();
	if (!Character || !GetWorld())
	{
		return;
	}

	const FMeleeChargeLevelData& Data = GetCurrentLevelData();

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MeleeChargeSweep), false);
	QueryParams.AddIgnoredActor(Character);
	if (AShooterWeapon* Weapon = GetCurrentWeapon())
	{
		QueryParams.AddIgnoredActor(Weapon);
	}

	TArray<FHitResult> Hits;
	const FCollisionShape Shape = FCollisionShape::MakeCapsule(Data.BashSweepRadius, Data.BashSweepHalfHeight);
	GetWorld()->SweepMultiByChannel(
		Hits,
		Start,
		End,
		FQuat::Identity,
		ECC_Pawn,
		Shape,
		QueryParams);

	if (Data.bDebugDrawChargeSweep)
	{
		DrawDebugCapsule(GetWorld(), End, Data.BashSweepHalfHeight, Data.BashSweepRadius, FQuat::Identity,
			Hits.Num() > 0 ? FColor::Red : FColor::Cyan, false, 0.05f, 0, 1.5f);
		DrawDebugLine(GetWorld(), Start, End, Hits.Num() > 0 ? FColor::Red : FColor::Cyan, false, 0.05f, 0, 2.0f);
	}

	for (const FHitResult& Hit : Hits)
	{
		AActor* HitActor = Hit.GetActor();
		if (!HitActor || HitActor == Character || HitActorsThisCharge.Contains(HitActor))
		{
			continue;
		}

		HitActorsThisCharge.Add(HitActor);
		ApplyBashDamage(HitActor, Hit);

		if (Data.bStopOnBash)
		{
			EndCharge(false);
			return;
		}
	}
}

void UUpgrade_MeleeCharge::ApplyBashDamage(AActor* Target, const FHitResult& Hit)
{
	if (!Target || !GetShooterCharacter())
	{
		return;
	}

	const FMeleeChargeLevelData& Data = GetCurrentLevelData();
	if (Data.BashDamage <= 0.0f)
	{
		ArmMeleeBoost();
		return;
	}

	AShooterCharacter* Character = GetShooterCharacter();
	AController* InstigatorController = Character ? Character->GetController() : nullptr;
	FPointDamageEvent DamageEvent(Data.BashDamage, Hit, ChargeDirection, Data.BashDamageType);
	Target->TakeDamage(Data.BashDamage, DamageEvent, InstigatorController, Character);

	const bool bKilled = IsActorDeadAfterDamage(Target);
	if (Character)
	{
		const FVector ImpactPoint(Hit.ImpactPoint);
		const FVector HitLocation = ImpactPoint.IsNearlyZero() ? Target->GetActorLocation() : ImpactPoint;
		Character->OnWeaponHit(HitLocation, ChargeDirection, Data.BashDamage, false, bKilled, Target);

		if (UUpgradeManagerComponent* UpgradeMgr = Character->GetUpgradeManager())
		{
			UpgradeMgr->NotifyOwnerDealtDamage(Target, Data.BashDamage, bKilled);
		}
	}

	ArmMeleeBoost();

	UE_LOG(LogTemp, Warning, TEXT("[MELEE_CHARGE] BASH %s damage=%.1f killed=%d"),
		*Target->GetName(), Data.BashDamage, bKilled ? 1 : 0);
}

bool UUpgrade_MeleeCharge::IsActorDeadAfterDamage(AActor* Actor) const
{
	if (!IsValid(Actor))
	{
		return true;
	}

	if (const AShooterNPC* NPC = Cast<AShooterNPC>(Actor))
	{
		return NPC->IsDead();
	}

	if (const AShooterCharacter* Character = Cast<AShooterCharacter>(Actor))
	{
		return Character->IsDead();
	}

	if (const AShooterDummy* Dummy = Cast<AShooterDummy>(Actor))
	{
		return Dummy->IsDead();
	}

	return Actor->IsPendingKillPending();
}

void UUpgrade_MeleeCharge::HandleMeleeWeaponHit(AActor* /*HitActor*/, const FVector& /*HitLocation*/, bool /*bHeadshot*/, float Damage)
{
	if (Damage > 0.0f && bPendingMeleeBoost)
	{
		ConsumeMeleeBoost();
	}
}
