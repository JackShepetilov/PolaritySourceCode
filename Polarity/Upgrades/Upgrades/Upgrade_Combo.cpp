// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "Upgrade_Combo.h"
#include "UpgradeDefinition_Combo.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/MeleeAttackComponent.h"
#include "Variant_Shooter/Weapons/ShooterWeapon_Melee.h"
#include "Curves/CurveFloat.h"

UUpgrade_Combo::UUpgrade_Combo()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UUpgrade_Combo::OnUpgradeActivated()
{
	CachedDef = Cast<UUpgradeDefinition_Combo>(UpgradeDefinition);
	if (!CachedDef.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[COMBO] Failed to cast UpgradeDefinition to Combo type"));
		return;
	}

	BindToMeleeSubsystem();
}

void UUpgrade_Combo::OnUpgradeDeactivated()
{
	UnbindFromMeleeSubsystem();

	// Make sure we don't leave the melee component / weapon stuck on an elevated speed.
	if (UMeleeAttackComponent* MeleeComp = CachedMeleeComp.Get())
	{
		MeleeComp->ApplyComboSpeedMultiplier(1.0f);
	}
	if (AShooterWeapon_Melee* MeleeWeapon = CachedMeleeWeapon.Get())
	{
		MeleeWeapon->ApplyComboSpeedMultiplier(1.0f);
	}

	SetComponentTickEnabled(false);
}

void UUpgrade_Combo::BindToMeleeSubsystem()
{
	AShooterCharacter* Character = GetShooterCharacter();
	if (!Character)
	{
		return;
	}

	UMeleeAttackComponent* MeleeComp = Character->GetMeleeAttackComponent();
	CachedMeleeComp = MeleeComp;
	if (MeleeComp)
	{
		MeleeComp->OnMeleeAttackStarted.AddDynamic(this, &UUpgrade_Combo::HandleMeleeAttackStarted);
		MeleeComp->OnMeleeHit.AddDynamic(this, &UUpgrade_Combo::HandleMeleeHit);
		MeleeComp->OnMeleeAttackEnded.AddDynamic(this, &UUpgrade_Combo::HandleMeleeAttackEnded);
	}

	// If a melee weapon is already equipped (e.g. upgrade granted mid-combat),
	// bind to it too so the sword respects combo as soon as it swings.
	if (AShooterWeapon_Melee* MeleeWeapon = Cast<AShooterWeapon_Melee>(Character->GetCurrentWeapon()))
	{
		BindToMeleeWeapon(MeleeWeapon);
	}
}

void UUpgrade_Combo::UnbindFromMeleeSubsystem()
{
	if (UMeleeAttackComponent* MeleeComp = CachedMeleeComp.Get())
	{
		MeleeComp->OnMeleeAttackStarted.RemoveDynamic(this, &UUpgrade_Combo::HandleMeleeAttackStarted);
		MeleeComp->OnMeleeHit.RemoveDynamic(this, &UUpgrade_Combo::HandleMeleeHit);
		MeleeComp->OnMeleeAttackEnded.RemoveDynamic(this, &UUpgrade_Combo::HandleMeleeAttackEnded);
	}
	CachedMeleeComp.Reset();

	if (AShooterWeapon_Melee* Weapon = CachedMeleeWeapon.Get())
	{
		UnbindFromMeleeWeapon(Weapon);
	}
	CachedMeleeWeapon.Reset();
}

void UUpgrade_Combo::OnWeaponChanged(AShooterWeapon* OldWeapon, AShooterWeapon* NewWeapon)
{
	// Swap our weapon binding without resetting the combo — combo persists across weapon swaps
	// (player can jab with fists, swap to sword, keep the chain going).
	if (AShooterWeapon_Melee* OldMelee = Cast<AShooterWeapon_Melee>(OldWeapon))
	{
		UnbindFromMeleeWeapon(OldMelee);
	}
	if (AShooterWeapon_Melee* NewMelee = Cast<AShooterWeapon_Melee>(NewWeapon))
	{
		BindToMeleeWeapon(NewMelee);
		// Push the current multiplier into the freshly equipped weapon so it starts at the right speed.
		NewMelee->ApplyComboSpeedMultiplier(CurrentMultiplier);
	}
	else
	{
		CachedMeleeWeapon.Reset();
	}
}

void UUpgrade_Combo::BindToMeleeWeapon(AShooterWeapon_Melee* Weapon)
{
	if (!Weapon)
	{
		return;
	}
	CachedMeleeWeapon = Weapon;
	Weapon->OnMeleeWeaponHit.AddDynamic(this, &UUpgrade_Combo::HandleSwordHit);
}

void UUpgrade_Combo::UnbindFromMeleeWeapon(AShooterWeapon_Melee* Weapon)
{
	if (!Weapon)
	{
		return;
	}
	Weapon->OnMeleeWeaponHit.RemoveDynamic(this, &UUpgrade_Combo::HandleSwordHit);
}

void UUpgrade_Combo::HandleMeleeAttackStarted()
{
	bHitThisSwing = false;
}

void UUpgrade_Combo::HandleMeleeHit(AActor* HitActor, const FVector& HitLocation, bool bHeadshot, float Damage)
{
	if (!CachedDef.IsValid() || Damage <= 0.0f)
	{
		return;
	}
	bHitThisSwing = true;
	AddComboHits(1);
}

void UUpgrade_Combo::HandleSwordHit(AActor* HitActor, const FVector& HitLocation, bool bHeadshot, float Damage)
{
	if (!CachedDef.IsValid() || Damage <= 0.0f)
	{
		return;
	}
	// Sword doesn't share MeleeAttackComponent's start/end events — treat each hit as both
	// a hit AND an implicit no-miss for the swing. Misses on sword are reset via timeout only.
	AddComboHits(1);
}

void UUpgrade_Combo::HandleMeleeAttackEnded()
{
	if (!CachedDef.IsValid() || !CachedDef->bResetOnMiss)
	{
		return;
	}

	// Fist swing finished without registering a hit → miss → reset.
	if (!bHitThisSwing && ComboCount > 0)
	{
		ResetCombo();
	}
}

void UUpgrade_Combo::AddComboHits(int32 Amount)
{
	if (Amount <= 0 || !CachedDef.IsValid())
	{
		return;
	}

	ComboCount += Amount;
	ResetTimer = CachedDef->ResetWindow;

	ApplyCurrentMultiplier();

	// Make sure tick is running so the reset timer counts down.
	SetComponentTickEnabled(true);
}

void UUpgrade_Combo::ApplyCurrentMultiplier()
{
	const float NewMultiplier = EvaluateMultiplier(ComboCount);
	CurrentMultiplier = NewMultiplier;

	if (UMeleeAttackComponent* MeleeComp = CachedMeleeComp.Get())
	{
		MeleeComp->ApplyComboSpeedMultiplier(NewMultiplier);
	}
	if (AShooterWeapon_Melee* MeleeWeapon = CachedMeleeWeapon.Get())
	{
		MeleeWeapon->ApplyComboSpeedMultiplier(NewMultiplier);
	}

	OnComboChanged.Broadcast(ComboCount, NewMultiplier);
}

void UUpgrade_Combo::ResetCombo()
{
	ComboCount = 0;
	ResetTimer = -1.0f;
	ApplyCurrentMultiplier(); // pushes 1.0x downstream via curve evaluation at 0

	SetComponentTickEnabled(false);
}

float UUpgrade_Combo::EvaluateMultiplier(int32 Count) const
{
	if (!CachedDef.IsValid())
	{
		return 1.0f;
	}

	float Value = 1.0f;
	if (CachedDef->ComboCountToMultiplier)
	{
		Value = CachedDef->ComboCountToMultiplier->GetFloatValue(static_cast<float>(Count));
	}
	else
	{
		// Fallback linear: +10% per hit, capped by MaxMultiplier.
		Value = 1.0f + 0.1f * Count;
	}

	return FMath::Clamp(Value, 1.0f, CachedDef->MaxMultiplier);
}

void UUpgrade_Combo::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (ResetTimer <= 0.0f)
	{
		return;
	}

	ResetTimer -= DeltaTime;
	if (ResetTimer <= 0.0f)
	{
		ResetCombo();
	}
}
