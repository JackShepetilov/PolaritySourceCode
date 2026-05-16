// HumanoidNPC.cpp

#include "HumanoidNPC.h"
#include "EMFVelocityModifier.h"
#include "NPCRiotShieldComponent.h"
#include "Variant_Shooter/Weapons/DroppedRangedWeapon.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "ShooterWeapon.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

AHumanoidNPC::AHumanoidNPC(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ShieldComponent = CreateDefaultSubobject<UNPCRiotShieldComponent>(TEXT("ShieldComponent"));
}

void AHumanoidNPC::BeginPlay()
{
	// MeleeNPC::BeginPlay sets WeaponClass = nullptr before calling Super, so we cannot
	// rely on WeaponClass for our inventory. Let Super run (spawns null weapon, that's fine),
	// then we manually spawn WeaponInventory[0] ourselves.
	Super::BeginPlay();

	// Cache defaults BEFORE any potential EnterMeleeMode call so we can restore on pool reset
	if (USkeletalMeshComponent* MeshComp = GetMesh())
	{
		CachedRangedAnimClass = MeshComp->GetAnimClass();
	}
	if (UCharacterMovementComponent* CMC = GetCharacterMovement())
	{
		CachedRangedMaxWalkSpeed = CMC->MaxWalkSpeed;
	}

	if (WeaponInventory.IsValidIndex(0) && WeaponInventory[0])
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = this;
		SpawnParams.Instigator = this;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		Weapon = GetWorld()->SpawnActor<AShooterWeapon>(WeaponInventory[0], GetActorTransform(), SpawnParams);
		if (Weapon)
		{
			Weapon->OnShotFired.AddDynamic(this, &AHumanoidNPC::OnWeaponShotFiredForward);
		}

		UE_LOG(LogTemp, Warning, TEXT("[HUMANOID_DEBUG] %s BeginPlay: WeaponInventory size=%d, spawned weapon=%s"),
			*GetName(), WeaponInventory.Num(), *GetNameSafe(Weapon));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[HUMANOID_DEBUG] %s BeginPlay: WeaponInventory empty — entering melee mode immediately"), *GetName());
		EnterMeleeMode();
	}

	// Disable body capture — player interacts with weapon (yank), not the body
	if (EMFVelocityModifier)
	{
		EMFVelocityModifier->bEnableViscousCapture = false;
	}
}

void AHumanoidNPC::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorld()->GetTimerManager().ClearTimer(WeaponSwitchTimer);

	// Unsubscribe from charge lock if still in melee mode
	if (bIsInMeleeMode)
	{
		OnChargeUpdated.RemoveDynamic(this, &AHumanoidNPC::OnChargeUpdatedInMeleeMode);
	}

	Super::EndPlay(EndPlayReason);
}

// ==================== Damage / Charge Protection ====================

float AHumanoidNPC::TakeDamage(float Damage, FDamageEvent const& DamageEvent,
	AController* EventInstigator, AActor* DamageCauser)
{
	if (!bChargeReceptive && EMFVelocityModifier)
	{
		// Save charge before parent logic applies any charge transfer
		const float SavedCharge = EMFVelocityModifier->GetCharge(); // in melee mode this is 0

		const float Result = Super::TakeDamage(Damage, DamageEvent, EventInstigator, DamageCauser);

		// If parent modified charge, restore it (in melee mode we always want 0)
		if (!FMath::IsNearlyEqual(EMFVelocityModifier->GetCharge(), SavedCharge))
		{
			EMFVelocityModifier->SetCharge(SavedCharge);
			UE_LOG(LogTemp, Warning, TEXT("[HUMANOID_DEBUG] %s: TakeDamage charge restored (melee mode)"), *GetName());
		}

		// Post-death cleanup (Die() is non-virtual, check bIsDead after Super returns)
		if (bIsDead)
		{
			GetWorld()->GetTimerManager().ClearTimer(WeaponSwitchTimer);
			ActiveYankMontage = nullptr;
		}

		return Result;
	}

	const float Result = Super::TakeDamage(Damage, DamageEvent, EventInstigator, DamageCauser);

	if (bIsDead)
	{
		GetWorld()->GetTimerManager().ClearTimer(WeaponSwitchTimer);
		ActiveYankMontage = nullptr;
	}

	return Result;
}

// ==================== EMF / Capture Immunity ====================

void AHumanoidNPC::OnWeaponShotFiredForward()
{
	// Relay to base class burst-fire logic (OnWeaponShotFired is protected — call via derived context)
	OnWeaponShotFired();
}

void AHumanoidNPC::ApplyKnockback(const FVector& KnockbackDir, float Distance, float Duration,
	const FVector& AttackerLocation, bool bKeepEMFEnabled, EKnockbackStyle Style)
{
	// Tractor Beam pull bypasses humanoid's body-knockback immunity — the upgrade explicitly
	// targets humanoids too. Other knockback sources (melee, explosion knockback) remain immune.
	if (Style == EKnockbackStyle::Tractor)
	{
		Super::ApplyKnockback(KnockbackDir, Distance, Duration, AttackerLocation, bKeepEMFEnabled, Style);
		return;
	}
	UE_LOG(LogTemp, Verbose, TEXT("[HUMANOID_DEBUG] %s: ApplyKnockback ignored (immune)"), *GetName());
}

void AHumanoidNPC::ApplyKnockbackVelocity(const FVector& KnockbackVelocity, float StunDuration)
{
	UE_LOG(LogTemp, Verbose, TEXT("[HUMANOID_DEBUG] %s: ApplyKnockbackVelocity ignored (immune)"), *GetName());
}

void AHumanoidNPC::EnterCapturedState(UAnimMontage* OverrideMontage)
{
	UE_LOG(LogTemp, Verbose, TEXT("[HUMANOID_DEBUG] %s: EnterCapturedState ignored (immune)"), *GetName());
}

void AHumanoidNPC::EnterLaunchedState()
{
	UE_LOG(LogTemp, Verbose, TEXT("[HUMANOID_DEBUG] %s: EnterLaunchedState ignored (immune)"), *GetName());
}

// ==================== Yank API ====================

bool AHumanoidNPC::CanBeYanked() const
{
	if (bIsInMeleeMode || bIsDead) return false;
	if (!Weapon) return false;
	if (GetWorld()->GetTimerManager().IsTimerActive(WeaponSwitchTimer)) return false;
	// Shield-first ordering: weapon stays unyankable until the shield (if any) is gone
	if (ShieldComponent && ShieldComponent->HasActiveShield()) return false;
	return true;
}

// ==================== Shield API ====================

bool AHumanoidNPC::CanShieldBeYanked() const
{
	if (bIsInMeleeMode || bIsDead) return false;
	if (!ShieldComponent || !ShieldComponent->HasActiveShield()) return false;
	return true;
}

bool AHumanoidNPC::YankShield(AShooterCharacter* Puller)
{
	if (!CanShieldBeYanked() || !Puller) return false;

	const bool bYanked = ShieldComponent->TryYank(Puller);

	if (bYanked)
	{
		// Mirror the weapon-yank charge reset so the body can re-charge for the next yank target
		if (EMFVelocityModifier)
		{
			EMFVelocityModifier->SetCharge(0.0f);
		}

		// Reuse the same directional yank reaction the weapon-yank uses — keeps shield/weapon yanks visually consistent
		if (UAnimMontage* Montage = SelectYankMontageForDirection(Puller->GetActorLocation()))
		{
			if (USkeletalMeshComponent* YankMesh = GetMesh())
			{
				if (UAnimInstance* AnimInst = YankMesh->GetAnimInstance())
				{
					AnimInst->Montage_Play(Montage);
					ActiveYankMontage = Montage;
				}
			}
		}
	}

	return bYanked;
}

float AHumanoidNPC::CalculateShieldYankRange() const
{
	if (!CanShieldBeYanked()) return 0.0f;
	return ShieldComponent->CalculateYankRange();
}

bool AHumanoidNPC::YankCurrentWeapon(AShooterCharacter* Puller)
{
	if (!CanBeYanked() || !Puller) return false;

	// 1. Spawn DroppedRangedWeapon at weapon's THIRD-PERSON MESH transform (the visible
	// mesh attached to NPC's hand socket). Actor transform of attached AShooterWeapon may
	// be at origin/identity since root is just a scene component. Mesh component holds the
	// actual world position seen by player. Fallback to actor transform if mesh missing.
	USkeletalMeshComponent* TPMesh = Weapon->GetThirdPersonMesh();
	const FVector WeaponLocation = TPMesh ? TPMesh->GetComponentLocation() : Weapon->GetActorLocation();
	const FRotator WeaponRotation = TPMesh ? TPMesh->GetComponentRotation() : Weapon->GetActorRotation();

	ADroppedRangedWeapon* Dropped = nullptr;
	if (WeaponDropMapping.IsValidIndex(CurrentWeaponIndex) && WeaponDropMapping[CurrentWeaponIndex])
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		Dropped = GetWorld()->SpawnActor<ADroppedRangedWeapon>(
			WeaponDropMapping[CurrentWeaponIndex],
			WeaponLocation,
			WeaponRotation,
			Params);

		if (Dropped)
		{
			// Match charge for visual widget display
			Dropped->SetCharge(EMFVelocityModifier ? EMFVelocityModifier->GetCharge() : 0.0f);

			// Roll ammo distribution: yank-spawned drops have limited ammo (curve or random
			// fallback). Death drops in ShooterNPC::Die() do NOT call this, so their granted
			// weapons stay at full mag with infinite refills.
			Dropped->RollSpawnedBulletCount();

			Dropped->StartPull(Puller);
		}
	}

	// 2. Zero body charge
	if (EMFVelocityModifier)
	{
		EMFVelocityModifier->SetCharge(0.0f);
	}

	// 3. Destroy current weapon (hands appear empty during animation)
	DespawnCurrentWeapon();

	// 4. Play directional yank animation
	if (UAnimMontage* Montage = SelectYankMontageForDirection(Puller->GetActorLocation()))
	{
		if (USkeletalMeshComponent* YankMesh = GetMesh())
		{
			if (UAnimInstance* AnimInst = YankMesh->GetAnimInstance())
			{
				AnimInst->Montage_Play(Montage);
				ActiveYankMontage = Montage;
			}
		}
	}

	// 5. Schedule next weapon spawn after animation
	GetWorld()->GetTimerManager().SetTimer(
		WeaponSwitchTimer,
		this, &AHumanoidNPC::SpawnNextWeapon,
		WeaponSwitchDelay, false);

	UE_LOG(LogTemp, Warning, TEXT("[HUMANOID_DEBUG] %s: Yanked weapon idx=%d, next spawn in %.2fs"),
		*GetName(), CurrentWeaponIndex, WeaponSwitchDelay);

	return true;
}

float AHumanoidNPC::CalculateWeaponYankRange() const
{
	if (!CanBeYanked()) return 0.0f;

	float BaseRange = 500.0f;
	float NormCoeff = 50.0f;

	// Read capture parameters from the CDO of the drop class at current index
	if (WeaponDropMapping.IsValidIndex(CurrentWeaponIndex) && WeaponDropMapping[CurrentWeaponIndex])
	{
		if (const ADroppedRangedWeapon* CDO = WeaponDropMapping[CurrentWeaponIndex]->GetDefaultObject<ADroppedRangedWeapon>())
		{
			BaseRange = CDO->CaptureBaseRange;
			NormCoeff = CDO->CaptureChargeNormCoeff;
		}
	}

	const float NpcChargeAbs = EMFVelocityModifier ? FMath::Abs(EMFVelocityModifier->GetCharge()) : 0.0f;
	if (NpcChargeAbs < KINDA_SMALL_NUMBER) return 0.0f;

	// Get player charge (same pattern as DroppedRangedWeapon::CalculateCaptureRange)
	float PlayerChargeAbs = 0.0f;
	if (ACharacter* PlayerChar = UGameplayStatics::GetPlayerCharacter(GetWorld(), 0))
	{
		if (UEMFVelocityModifier* PlayerMod = PlayerChar->FindComponentByClass<UEMFVelocityModifier>())
		{
			PlayerChargeAbs = FMath::Abs(PlayerMod->GetCharge());
		}
	}
	if (PlayerChargeAbs < KINDA_SMALL_NUMBER) return 0.0f;

	const float Ratio = (PlayerChargeAbs * NpcChargeAbs) / FMath::Max(NormCoeff, 0.01f);
	const float RangeMultiplier = FMath::Max(1.0f, 1.0f + FMath::Loge(Ratio));
	return BaseRange * RangeMultiplier;
}

// ==================== Internal Weapon Management ====================

void AHumanoidNPC::SpawnNextWeapon()
{
	if (bIsDead) return;

	CurrentWeaponIndex++;

	if (!WeaponInventory.IsValidIndex(CurrentWeaponIndex) || !WeaponInventory[CurrentWeaponIndex])
	{
		EnterMeleeMode();
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.Instigator = this;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	Weapon = GetWorld()->SpawnActor<AShooterWeapon>(
		WeaponInventory[CurrentWeaponIndex],
		GetActorTransform(),
		SpawnParams);

	if (Weapon)
	{
		Weapon->OnShotFired.AddDynamic(this, &AHumanoidNPC::OnWeaponShotFiredForward);
	}

	UE_LOG(LogTemp, Warning, TEXT("[HUMANOID_DEBUG] %s: Spawned next weapon idx=%d"), *GetName(), CurrentWeaponIndex);
}

void AHumanoidNPC::DespawnCurrentWeapon()
{
	if (Weapon)
	{
		Weapon->Destroy();
		Weapon = nullptr;
	}
}

void AHumanoidNPC::EnterMeleeMode()
{
	bIsInMeleeMode = true;
	bChargeReceptive = false;

	DespawnCurrentWeapon();

	// Zero charge and lock it on 0 via delegate subscription
	if (EMFVelocityModifier)
	{
		EMFVelocityModifier->SetCharge(0.0f);
	}

	OnChargeUpdated.AddDynamic(this, &AHumanoidNPC::OnChargeUpdatedInMeleeMode);

	// Apply melee AnimBP and walk speed (both optional — null/0 means keep ranged values)
	if (MeleeAnimClass)
	{
		if (USkeletalMeshComponent* MeshComp = GetMesh())
		{
			MeshComp->SetAnimInstanceClass(MeleeAnimClass);
		}
	}
	if (MeleeMaxWalkSpeed > 0.0f)
	{
		if (UCharacterMovementComponent* CMC = GetCharacterMovement())
		{
			CMC->MaxWalkSpeed = MeleeMaxWalkSpeed;
		}
	}

	OnEnteredMeleeMode.Broadcast(this);

	UE_LOG(LogTemp, Warning, TEXT("[HUMANOID_DEBUG] %s: Entered melee mode (no more charge intake)"), *GetName());
}

void AHumanoidNPC::OnChargeUpdatedInMeleeMode(float ChargeValue, uint8 Polarity)
{
	// Only react when charge drifts away from 0 (prevents no-op spam)
	if (!FMath::IsNearlyZero(ChargeValue) && EMFVelocityModifier)
	{
		EMFVelocityModifier->SetCharge(0.0f);
	}
}

UAnimMontage* AHumanoidNPC::SelectYankMontageForDirection(const FVector& PullerLocation) const
{
	const FVector ToPuller = (PullerLocation - GetActorLocation()).GetSafeNormal();
	const float ForwardDot = FVector::DotProduct(GetActorForwardVector(), ToPuller);
	const float RightDot   = FVector::DotProduct(GetActorRightVector(),   ToPuller);

	// Signed angle: positive = Puller is to the right of NPC, negative = to the left
	const float AngleDeg = FMath::RadiansToDegrees(FMath::Atan2(RightDot, ForwardDot));

	if (FMath::Abs(AngleDeg) < 45.0f)  return YankFrontMontage;
	if (FMath::Abs(AngleDeg) > 135.0f) return YankBackMontage;
	if (AngleDeg > 0.0f)               return YankRightMontage;
	return YankLeftMontage;
}

// ==================== Pool Reset ====================

void AHumanoidNPC::ResetForPool(const FVector& NewLocation, const FRotator& NewRotation)
{
	const bool bWasInMeleeMode = bIsInMeleeMode;

	CurrentWeaponIndex = 0;

	if (bWasInMeleeMode)
	{
		OnChargeUpdated.RemoveDynamic(this, &AHumanoidNPC::OnChargeUpdatedInMeleeMode);
	}

	bIsInMeleeMode = false;
	bChargeReceptive = true;
	ActiveYankMontage = nullptr;

	GetWorld()->GetTimerManager().ClearTimer(WeaponSwitchTimer);

	// Cleanup any leftover weapon actor (Yank flow may have left a stale pointer if NPC died mid-switch)
	DespawnCurrentWeapon();

	// Restore ranged AnimBP and walk speed BEFORE Super (Super calls MeshComp->InitAnim — picks up the swapped class)
	if (bWasInMeleeMode)
	{
		if (CachedRangedAnimClass)
		{
			if (USkeletalMeshComponent* MeshComp = GetMesh())
			{
				MeshComp->SetAnimInstanceClass(CachedRangedAnimClass);
			}
		}
		if (CachedRangedMaxWalkSpeed > 0.0f)
		{
			if (UCharacterMovementComponent* CMC = GetCharacterMovement())
			{
				CMC->MaxWalkSpeed = CachedRangedMaxWalkSpeed;
			}
		}
	}

	// Re-enable body capture flag (reset by parent before re-spawn)
	if (EMFVelocityModifier)
	{
		EMFVelocityModifier->bEnableViscousCapture = false; // humanoids are always immune to body capture
	}

	// Reset MeleeNPC parent state vars — AShooterNPC::ResetForPool does NOT touch them.
	// If NPC died mid-attack/dash in melee mode, these flags would persist:
	//   bIsAttacking=true → CanAttack() always returns false → no melee attacks ever
	//   bDamageWindowActive=true → PerformMeleeTrace runs every Tick → invisible damage on respawn
	//   bIsDashing=true → UpdateDashInterpolation teleports NPC from old DashStartPosition
	bIsAttacking = false;
	bDamageWindowActive = false;
	bAttackOnCooldown = false;
	bHasDealtDamage = false;
	bIsDashing = false;
	LastAttackTime = -1.0f;
	LastDashTime = -1.0f;
	DashElapsedTime = 0.0f;
	DashTotalDuration = 0.0f;
	HitActorsThisAttack.Empty();
	CurrentMeleeTarget = nullptr;
	DashTargetActor = nullptr;

	// Clear MeleeNPC timers (parent's ResetForPool clears its own but not these)
	UWorld* World = GetWorld();
	FTimerManager& TM = World->GetTimerManager();
	TM.ClearTimer(DamageWindowStartTimer);
	TM.ClearTimer(DamageWindowEndTimer);
	TM.ClearTimer(AttackCooldownTimer);

	// Parent resets HP, charge, AI, etc.
	Super::ResetForPool(NewLocation, NewRotation);

	// Re-activate shield if it was yanked during the previous life. No-op for vanilla BPs without shield asset.
	if (ShieldComponent)
	{
		ShieldComponent->ResetForPool();
	}

	// Spawn first weapon fresh from inventory
	if (WeaponInventory.IsValidIndex(0) && WeaponInventory[0])
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = this;
		SpawnParams.Instigator = this;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		Weapon = GetWorld()->SpawnActor<AShooterWeapon>(WeaponInventory[0], GetActorTransform(), SpawnParams);
		if (Weapon)
		{
			Weapon->OnShotFired.AddDynamic(this, &AHumanoidNPC::OnWeaponShotFiredForward);
		}

		// Notify BP if we just transitioned out of melee mode (e.g. died disarmed, recycled with weapons)
		if (bWasInMeleeMode)
		{
			OnExitedMeleeMode.Broadcast(this);
		}
	}
	else
	{
		// No inventory configured — re-enter melee mode (re-fires OnEnteredMeleeMode if BP rebound)
		EnterMeleeMode();
	}
}
