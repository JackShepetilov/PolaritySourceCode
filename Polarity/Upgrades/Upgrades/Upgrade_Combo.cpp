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
		UE_LOG(LogTemp, Error, TEXT("[COMBO_DEBUG] Failed to cast UpgradeDefinition to Combo type"));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[COMBO_DEBUG] ACTIVATED — ResetWindow=%.2fs, MaxMult=%.2f, bResetOnMiss=%d, Curve=%s"),
		CachedDef->ResetWindow,
		CachedDef->MaxMultiplier,
		CachedDef->bResetOnMiss ? 1 : 0,
		CachedDef->ComboCountToMultiplier ? *CachedDef->ComboCountToMultiplier->GetName() : TEXT("<fallback linear>"));

	BindToMeleeSubsystem();
}

void UUpgrade_Combo::OnUpgradeDeactivated()
{
	UE_LOG(LogTemp, Warning, TEXT("[COMBO_DEBUG] DEACTIVATED — final Count=%d, Mult=%.2f"), ComboCount, CurrentMultiplier);

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
	const bool bHadMeleeWeapon = (Cast<AShooterWeapon_Melee>(OldWeapon) != nullptr);
	const bool bHasMeleeWeapon = (Cast<AShooterWeapon_Melee>(NewWeapon) != nullptr);
	UE_LOG(LogTemp, Verbose, TEXT("[COMBO_DEBUG] WeaponChanged: had-melee=%d -> has-melee=%d, preserving combo Count=%d Mult=%.2f"),
		bHadMeleeWeapon ? 1 : 0, bHasMeleeWeapon ? 1 : 0, ComboCount, CurrentMultiplier);

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
	UE_LOG(LogTemp, Verbose, TEXT("[COMBO_DEBUG] FistSwing STARTED (Count=%d)"), ComboCount);
}

void UUpgrade_Combo::HandleMeleeHit(AActor* HitActor, const FVector& HitLocation, bool bHeadshot, float Damage)
{
	if (!CachedDef.IsValid() || Damage <= 0.0f)
	{
		return;
	}
	bHitThisSwing = true;
	UE_LOG(LogTemp, Warning, TEXT("[COMBO_DEBUG] Fist HIT %s (dmg=%.1f) +1"),
		HitActor ? *HitActor->GetName() : TEXT("NULL"), Damage);
	AddComboHits(1);
}

void UUpgrade_Combo::HandleSwordHit(AActor* HitActor, const FVector& HitLocation, bool bHeadshot, float Damage)
{
	if (!CachedDef.IsValid() || Damage <= 0.0f)
	{
		return;
	}
	UE_LOG(LogTemp, Warning, TEXT("[COMBO_DEBUG] Sword HIT %s (dmg=%.1f) +1"),
		HitActor ? *HitActor->GetName() : TEXT("NULL"), Damage);
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
		UE_LOG(LogTemp, Warning, TEXT("[COMBO_DEBUG] FistSwing ENDED with MISS — combo RESET (was Count=%d)"), ComboCount);
		ResetCombo();
	}
	else
	{
		UE_LOG(LogTemp, Verbose, TEXT("[COMBO_DEBUG] FistSwing ENDED (hit=%d, Count=%d kept)"), bHitThisSwing ? 1 : 0, ComboCount);
	}
}

void UUpgrade_Combo::AddComboHits(int32 Amount)
{
	if (Amount <= 0 || !CachedDef.IsValid())
	{
		return;
	}

	const int32 OldCount = ComboCount;
	ComboCount += Amount;
	ResetTimer = CachedDef->ResetWindow;

	UE_LOG(LogTemp, Warning, TEXT("[COMBO_DEBUG] AddComboHits(+%d): Count %d -> %d, ResetTimer=%.2fs"),
		Amount, OldCount, ComboCount, ResetTimer);

	ApplyCurrentMultiplier();

	// Make sure tick is running so the reset timer counts down.
	SetComponentTickEnabled(true);
}

void UUpgrade_Combo::ApplyCurrentMultiplier()
{
	const float OldMultiplier = CurrentMultiplier;
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

	UE_LOG(LogTemp, Warning, TEXT("[COMBO_DEBUG] Mult %.2fx -> %.2fx (Count=%d, fist=%s, sword=%s)"),
		OldMultiplier, NewMultiplier, ComboCount,
		CachedMeleeComp.IsValid() ? TEXT("OK") : TEXT("NULL"),
		CachedMeleeWeapon.IsValid() ? TEXT("OK") : TEXT("NULL"));

	OnComboChanged.Broadcast(ComboCount, NewMultiplier);
}

void UUpgrade_Combo::ResetCombo()
{
	UE_LOG(LogTemp, Warning, TEXT("[COMBO_DEBUG] RESET — Count %d -> 0"), ComboCount);
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
