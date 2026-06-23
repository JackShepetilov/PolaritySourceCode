// DroppedRangedWeapon.cpp

#include "DroppedRangedWeapon.h"
#include "ChargeAnimationComponent.h"
#include "ShooterWeapon.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "Variant_Shooter/UI/EMFChargeWidgetSubsystem.h"
#include "EMF_FieldComponent.h"
#include "EMFVelocityModifier.h"
#include "Upgrades/UpgradeManagerComponent.h"
#include "Upgrades/Upgrades/Upgrade_AirKick.h"
#include "Upgrades/Upgrades/AirMailSpear.h"
#include "Components/StaticMeshComponent.h"
#include "Curves/CurveFloat.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/DamageEvents.h"

ADroppedRangedWeapon::ADroppedRangedWeapon()
{
	PrimaryActorTick.bCanEverTick = true;

	// Weapon mesh — root, physics-simulated
	WeaponMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WeaponMesh"));
	SetRootComponent(WeaponMesh);
	WeaponMesh->SetSimulatePhysics(true);
	WeaponMesh->SetCollisionProfileName(FName("PhysicsActor"));
	WeaponMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	WeaponMesh->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	WeaponMesh->SetGenerateOverlapEvents(true);
	WeaponMesh->BodyInstance.bUseCCD = true;

	// EMF field component for charge storage
	FieldComponent = CreateDefaultSubobject<UEMF_FieldComponent>(TEXT("FieldComponent"));
}

void ADroppedRangedWeapon::BeginPlay()
{
	Super::BeginPlay();

	UE_LOG(LogTemp, Warning, TEXT("[DroppedRangedWeapon] %s BeginPlay: Charge=%.2f, bCanBeCaptured=%d"),
		*GetName(), GetCharge(), bCanBeCaptured);

	// Bind hit callback for stun-on-impact. The callback gates on bCanStunOnImpact at runtime,
	// so we always bind (cheap) regardless of whether stun is currently enabled.
	if (WeaponMesh)
	{
		WeaponMesh->SetNotifyRigidBodyCollision(true);
		WeaponMesh->OnComponentHit.AddDynamic(this, &ADroppedRangedWeapon::OnWeaponMeshHit);
	}

	// Opt-in yank-style limited ammo for death drops. Skip if the yank path already rolled
	// (SpawnedBulletCount >= 0 means RollSpawnedBulletCount ran before BeginPlay, e.g. via
	// SpawnActorDeferred — though current callers don't use that pattern).
	if (bForceLimitedAmmo && SpawnedBulletCount < 0)
	{
		RollSpawnedBulletCount();
		UE_LOG(LogTemp, Warning, TEXT("[YANK_AMMO] %s: bForceLimitedAmmo=true → auto-rolled SpawnedBulletCount=%d"),
			*GetName(), SpawnedBulletCount);
	}
}

void ADroppedRangedWeapon::OnWeaponMeshHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	if (!OtherActor || !WeaponMesh) return;

	// ==================== Air Mail: kicked flight resolves on first impact ====================
	// The kicked weapon deals the upgrade's KickDamage to the NPC it slams into (plus the
	// regular throw stun below, since bCanStunOnImpact is still set from the throw).
	if (ActorHasTag(UUpgrade_AirKick::TAG_AirMailKicked))
	{
		Tags.Remove(UUpgrade_AirKick::TAG_AirMailKicked);

		AShooterNPC* KickedNPC = Cast<AShooterNPC>(OtherActor);
		if (KickedNPC && !KickedNPC->IsDead())
		{
			if (UUpgrade_AirKick* AirMail = UUpgrade_AirKick::FindActiveAirMail(this))
			{
				AShooterCharacter* Player = AirMail->GetShooterCharacter();
				FPointDamageEvent KickDamageEvent;
				KickDamageEvent.DamageTypeClass = AirMail->GetKickDamageType();
				KickDamageEvent.HitInfo = Hit;
				KickedNPC->TakeDamage(AirMail->GetKickDamage(), KickDamageEvent,
					Player ? Player->GetController() : nullptr, Player);

				UE_LOG(LogTemp, Warning, TEXT("[AIR_MAIL] kicked weapon %s slammed %s for %.0f"),
					*GetName(), *KickedNPC->GetName(), AirMail->GetKickDamage());
			}
		}
		// Fall through: the throw stun below may still apply on the same impact.
	}
	// Returning flight that hits anything without being kicked just lands — clear the state.
	else if (ActorHasTag(UUpgrade_AirKick::TAG_AirMailIncoming))
	{
		Tags.Remove(UUpgrade_AirKick::TAG_AirMailIncoming);
	}

	if (!bCanStunOnImpact) return;

	const FVector ImpactVelocity = PreImpactVelocity;
	const float ImpactSpeed = ImpactVelocity.Size();

	// ==================== Throw stun (existing behavior) ====================
	// Cooldown + velocity gates prevent stun-spam from a settling/rolling weapon.
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now - LastStunTime >= StunCooldown && ImpactSpeed >= StunImpactVelocityThreshold)
	{
		// Only NPCs (not props, not the player). HumanoidNPC's ApplyExplosionStun is currently
		// no-op (immune to forces) — that's intentional per spec; passes through as no stun.
		AShooterNPC* NPC = Cast<AShooterNPC>(OtherActor);
		if (NPC && !NPC->IsDead())
		{
			NPC->ApplyExplosionStun(StunDuration, StunMontage);
			LastStunTime = Now;

			UE_LOG(LogTemp, Warning, TEXT("[YANK_THROW] %s stunned %s for %.1fs (impact speed=%.0f)"),
				*GetName(), *NPC->GetName(), StunDuration, ImpactSpeed);
		}
	}

	// ==================== Air Mail: bounce back to the player ====================
	// One bounce per throw; never off the player themselves. Angle/speed gates live in the
	// upgrade (60–120° incidence band — glancing slides don't return).
	if (!bAirMailBounceConsumed
		&& !OtherActor->IsA<AShooterCharacter>()
		&& !ActorHasTag(UUpgrade_AirKick::TAG_AirMailKicked))
	{
		if (UUpgrade_AirKick* AirMail = UUpgrade_AirKick::FindActiveAirMail(this))
		{
			// Thrown INTO a character: the capsule normal vs the throw's arced trajectory makes
			// the incidence angle meaningless — enemy hits always qualify (speed gate remains).
			const bool bCharacterImpact = OtherActor->IsA<APawn>();

			FVector ReturnVelocity;
			if (AirMail->TryComputeBounce(GetActorLocation(), ImpactVelocity, Hit.ImpactNormal, ReturnVelocity, bCharacterImpact))
			{
				bAirMailBounceConsumed = true;
				WeaponMesh->SetPhysicsLinearVelocity(ReturnVelocity);
				if (AirMail->GetReturnSpinSpeed() > 0.0f)
				{
					AirMailOrientSpear(WeaponMesh, ReturnVelocity);
				}
				Tags.Add(UUpgrade_AirKick::TAG_AirMailIncoming);
				AirMail->PlayBounceFeedback(Hit.ImpactPoint);

				UE_LOG(LogTemp, Warning, TEXT("[AIR_MAIL] thrown weapon %s bounced off %s toward player (impact speed=%.0f)"),
					*GetName(), *OtherActor->GetName(), ImpactSpeed);
			}
		}
	}
}

void ADroppedRangedWeapon::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Cache pre-contact velocity for OnWeaponMeshHit — at hit-callback time the physics solver
	// has already altered the velocity, which breaks the Air Mail incidence-angle test.
	if (WeaponMesh && WeaponMesh->IsSimulatingPhysics())
	{
		PreImpactVelocity = WeaponMesh->GetPhysicsLinearVelocity();
	}

	// Air Mail spear: while kicked, drive orientation kinematically (nose along velocity + roll)
	// so the asymmetric body can't precess/tumble. Self-stops once the weapon slows down.
	if (ActorHasTag(UUpgrade_AirKick::TAG_AirMailKicked))
	{
		UUpgrade_AirKick* AirMail = UUpgrade_AirKick::FindActiveAirMail(this);
		AirMailTickSpear(WeaponMesh, AirMail ? AirMail->GetKickSpinSpeed() : 720.0f);
	}

	if (bIsBeingPulled)
	{
		UpdatePull(DeltaTime);
	}
}

// ==================== Charge API ====================

float ADroppedRangedWeapon::GetCharge() const
{
	if (FieldComponent)
	{
		FEMSourceDescription Desc = FieldComponent->GetSourceDescription();
		return Desc.PointChargeParams.Charge;
	}
	return 0.0f;
}

void ADroppedRangedWeapon::SetCharge(float NewCharge)
{
	if (FieldComponent)
	{
		FEMSourceDescription Desc = FieldComponent->GetSourceDescription();
		Desc.PointChargeParams.Charge = NewCharge;
		FieldComponent->SetSourceDescription(Desc);
	}

	// Register (or re-register) charge widget
	if (UEMFChargeWidgetSubsystem* WidgetSub = GetWorld()->GetSubsystem<UEMFChargeWidgetSubsystem>())
	{
		WidgetSub->UnregisterDroppedRangedWeapon(this);
		WidgetSub->RegisterDroppedRangedWeapon(this);
	}
}

// ==================== Ammo Distribution ====================

void ADroppedRangedWeapon::RollSpawnedBulletCount()
{
	if (!WeaponClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[YANK_AMMO] %s: RollSpawnedBulletCount skipped — WeaponClass is null"), *GetName());
		return;
	}

	const AShooterWeapon* CDO = WeaponClass->GetDefaultObject<AShooterWeapon>();
	const int32 MagSize = CDO ? CDO->GetMagazineSize() : 0;
	if (MagSize <= 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[YANK_AMMO] %s: RollSpawnedBulletCount skipped — invalid MagSize=%d"),
			*GetName(), MagSize);
		return;
	}

	int32 RolledCount;
	if (AmmoDistributionCurve)
	{
		// Inverse-transform sampling: roll random in [0..1], read fraction from curve
		const float Roll = FMath::FRand();
		const float Fraction = FMath::Clamp(AmmoDistributionCurve->GetFloatValue(Roll), 0.0f, 1.0f);
		RolledCount = FMath::RoundToInt(Fraction * MagSize);
		UE_LOG(LogTemp, Log, TEXT("[YANK_AMMO] %s: curve roll=%.3f, fraction=%.3f, count=%d"),
			*GetName(), Roll, Fraction, RolledCount);
	}
	else
	{
		// Fallback: uniform random [1, MagSize] inclusive
		RolledCount = FMath::RandRange(1, MagSize);
		UE_LOG(LogTemp, Log, TEXT("[YANK_AMMO] %s: no curve, random fallback count=%d (range 1..%d)"),
			*GetName(), RolledCount, MagSize);
	}

	// Clamp to [1, MagSize] — minimum 1 bullet so the pickup is always usable
	SpawnedBulletCount = FMath::Clamp(RolledCount, 1, MagSize);
}

// ==================== Capture Range ====================

float ADroppedRangedWeapon::CalculateCaptureRange() const
{
	return UChargeAnimationComponent::GetCaptureRangeFor(this, FMath::Abs(GetCharge()));
}

// ==================== Pull ====================

void ADroppedRangedWeapon::StartPull(AShooterCharacter* InPullingPlayer)
{
	if (!InPullingPlayer || bIsBeingPulled || bPullComplete)
	{
		return;
	}

	bIsBeingPulled = true;
	PullElapsed = 0.0f;
	PullingCharacter = InPullingPlayer;
	PullStartLocation = GetActorLocation();
	PullStartRotation = GetActorRotation();

	// Snapshot the player's current weapon class for the Bandolier check at CompletePull —
	// the player may switch weapons mid-pull, but capacity is gated by what they had in hand
	// at the moment they committed to pulling this specific drop.
	if (const AShooterWeapon* CurrentHeld = InPullingPlayer->GetCurrentWeapon())
	{
		PullingClientCurrentWeaponClass = CurrentHeld->GetClass();
	}

	// Disable physics — we drive position directly
	WeaponMesh->SetSimulatePhysics(false);
	WeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Stop exerting EMF force while under scripted pull. The drop spawns at its charged origin —
	// a yank spawns it AT the boss carrying the boss's charge — and is flown to the player by
	// SetActorLocation. While its field stays registered, that live charge attracts the player
	// toward the drop's origin (the boss), because the player filters NPC-typed sources
	// (NPCForceMultiplier=0) but NOT this drop's source. The charge value remains in SourceParams
	// for GetCharge()/widget/capture-range queries (those read it locally, not via the registry),
	// and the capture scan already skips weapons that are being pulled.
	if (FieldComponent)
	{
		FieldComponent->UnregisterFromRegistry();
	}
}

void ADroppedRangedWeapon::UpdatePull(float DeltaTime)
{
	if (!PullingCharacter.IsValid())
	{
		// Player gone — drop the weapon back
		bIsBeingPulled = false;
		WeaponMesh->SetSimulatePhysics(true);
		WeaponMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		// Restore EMF source registration (unregistered in StartPull) now that it's a world drop again.
		if (FieldComponent)
		{
			FieldComponent->RegisterWithRegistry();
		}
		return;
	}

	PullElapsed += DeltaTime;
	const float Alpha = FMath::Clamp(PullElapsed / PullDuration, 0.0f, 1.0f);
	const float CurvedAlpha = FMath::InterpEaseInOut(0.0f, 1.0f, Alpha, 2.0f);

	// Calculate camera-relative target in world space
	APlayerCameraManager* CamMgr = UGameplayStatics::GetPlayerCameraManager(GetWorld(), 0);
	if (!CamMgr)
	{
		return;
	}

	const FVector CameraLoc = CamMgr->GetCameraLocation();
	const FRotator CameraRot = CamMgr->GetCameraRotation();

	// Transform offset into world space relative to camera
	const FVector WorldTarget = CameraLoc
		+ CameraRot.RotateVector(PullTargetOffset);
	const FRotator WorldTargetRot = CameraRot + PullTargetRotation;

	// Interpolate
	const FVector NewPos = FMath::Lerp(PullStartLocation, WorldTarget, CurvedAlpha);
	const FRotator NewRot = FMath::Lerp(PullStartRotation, WorldTargetRot, CurvedAlpha);
	SetActorLocation(NewPos);
	SetActorRotation(NewRot);

	if (Alpha >= 1.0f)
	{
		CompletePull();
	}
}

void ADroppedRangedWeapon::CompletePull()
{
	bPullComplete = true;
	bIsBeingPulled = false;

	// Unregister charge widget
	if (UEMFChargeWidgetSubsystem* WidgetSub = GetWorld()->GetSubsystem<UEMFChargeWidgetSubsystem>())
	{
		WidgetSub->UnregisterDroppedRangedWeapon(this);
	}

	// Hide this actor
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);

	if (!PullingCharacter.IsValid() || !WeaponClass)
	{
		Destroy();
		return;
	}

	AShooterCharacter* Player = PullingCharacter.Get();

	// Play pickup sound
	if (PickupSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, PickupSound, GetActorLocation());
	}

	// Check if player already has a weapon of this class — if so, skip (no stacking for ranged)
	AShooterWeapon* ExistingWeapon = Player->FindWeaponOfType(WeaponClass);

	UE_LOG(LogTemp, Warning, TEXT("[PICKUP_DEBUG] CompletePull (DROPPED): WeaponClass=%s, ExistingWeapon=%s, SpawnedBulletCount=%d"),
		*GetNameSafe(WeaponClass), *GetNameSafe(ExistingWeapon), SpawnedBulletCount);

	if (!ExistingWeapon)
	{
		// Grant a new weapon (permanent) with animated lower→swap→raise transition.
		// AddWeaponClassAnimated falls back to instant equip if player is unarmed.
		Player->AddWeaponClassAnimated(WeaponClass);

		// Tag the freshly-added weapon as yank-acquired so the strict "one yanked weapon at a time"
		// rule (ThrowYankedWeaponIfAny) can identify and discard it on subsequent yanks.
		if (AShooterWeapon* AddedWeapon = Player->FindWeaponOfType(WeaponClass))
		{
			AddedWeapon->bWasYanked = true;
			AddedWeapon->SourceYankDropClass = GetClass();

			// Limited-ammo behavior: only set when this drop was yank-spawned (HumanoidNPC called
			// RollSpawnedBulletCount → SpawnedBulletCount > 0). Death drops leave SpawnedBulletCount
			// at the -1 default, so the granted weapon stays at full mag with infinite refills.
			if (SpawnedBulletCount > 0)
			{
				AddedWeapon->SetBulletCount(SpawnedBulletCount);
				AddedWeapon->bHasLimitedAmmo = true;
				UE_LOG(LogTemp, Warning, TEXT("[YANK_AMMO] CompletePull — %s granted with %d bullets (limited ammo)"),
					*AddedWeapon->GetName(), SpawnedBulletCount);
			}

			UE_LOG(LogTemp, Warning, TEXT("[YANK_THROW] CompletePull — tagged %s: bWasYanked=true, SourceYankDropClass=%s, bHasLimitedAmmo=%d"),
				*AddedWeapon->GetName(), *GetClass()->GetName(), AddedWeapon->bHasLimitedAmmo ? 1 : 0);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[YANK_THROW] CompletePull — FindWeaponOfType returned NULL after AddWeaponClassAnimated! Weapon class: %s"),
				*WeaponClass->GetName());
		}
	}
	else
	{
		// Player already has a weapon of this class. Bandolier opt-in: if the upgrade is
		// owned AND the player was holding this same class when StartPull fired, the drop
		// goes into reserve (or spills bullets on overflow). Otherwise: original skip.
		UUpgradeManagerComponent* UpgradeMgr = Player->GetUpgradeManager();
		const int32 MaxCopies = UpgradeMgr ? UpgradeMgr->GetBandolierMaxCopies() : 1;
		const bool bClassMatchAtPullStart =
			PullingClientCurrentWeaponClass && PullingClientCurrentWeaponClass == WeaponClass;

		if (MaxCopies > 1 && bClassMatchAtPullStart)
		{
			// Compute the bullet count we'd hand out — fall back to a full mag if the death-drop
			// path left SpawnedBulletCount at -1 (the player still gets ammo, not a dry weapon).
			const int32 MagSize = WeaponClass->GetDefaultObject<AShooterWeapon>()->GetMagazineSize();
			const int32 BulletsForCopy = (SpawnedBulletCount > 0) ? SpawnedBulletCount : MagSize;

			const int32 OwnedYankedCount = Player->CountYankedCopiesOfClass(WeaponClass);

			if (OwnedYankedCount < MaxCopies)
			{
				Player->AddYankedReserveCopy(WeaponClass, GetClass(), BulletsForCopy);
				UE_LOG(LogTemp, Warning, TEXT("[BANDOLIER] %s pickup → reserve (%d/%d copies of %s)"),
					*GetName(), OwnedYankedCount + 1, MaxCopies, *WeaponClass->GetName());
			}
			else
			{
				Player->SpillBulletsIntoYankedCopiesOfClass(WeaponClass, BulletsForCopy);
				UE_LOG(LogTemp, Warning, TEXT("[BANDOLIER] %s pickup → overflow spill of %d bullets (%d/%d copies, at cap)"),
					*GetName(), BulletsForCopy, OwnedYankedCount, MaxCopies);
			}
		}
		else
		{
			// Duplicate pickup of an already-owned class: instead of discarding the drop,
			// top up the existing weapon's magazine to full. SetBulletCount clamps to [0, MagazineSize].
			const int32 MaxAmmo = ExistingWeapon->GetMagazineSize();
			const int32 BeforeAmmo = ExistingWeapon->GetBulletCount();

			ExistingWeapon->SetBulletCount(MaxAmmo);

			// Refresh the ammo HUD only if this is the weapon currently in hand
			if (ExistingWeapon == Player->GetCurrentWeapon())
			{
				Player->UpdateWeaponHUD(ExistingWeapon->GetBulletCount(), MaxAmmo);
			}

			UE_LOG(LogTemp, Warning, TEXT("[PICKUP_DEBUG] DROPPED duplicate of %s — magazine topped up %d -> %d (MaxCopies=%d, classMatch=%d, IsCurrent=%d)"),
				*WeaponClass->GetName(), BeforeAmmo, ExistingWeapon->GetBulletCount(),
				MaxCopies, bClassMatchAtPullStart ? 1 : 0,
				(ExistingWeapon == Player->GetCurrentWeapon()) ? 1 : 0);
		}
	}

	// Destroy this world actor (weapon is now in player's inventory)
	Destroy();
}
