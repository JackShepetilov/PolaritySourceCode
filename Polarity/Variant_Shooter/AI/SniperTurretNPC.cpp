// SniperTurretNPC.cpp

#include "SniperTurretNPC.h"
#include "ShooterWeapon.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TimerManager.h"
#include "Engine/DamageEvents.h"
#include "Engine/SkeletalMesh.h"
#include "DamageTypes/DamageType_Melee.h"
#include "EMFVelocityModifier.h"

ASniperTurretNPC::ASniperTurretNPC(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Create turret skeletal mesh (PoseableMesh for direct bone rotation)
	TurretMesh = CreateDefaultSubobject<UPoseableMeshComponent>(TEXT("TurretMesh"));
	TurretMesh->SetupAttachment(RootComponent);
	TurretMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Hide the inherited SkeletalMesh
	GetMesh()->SetVisibility(false);
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Disable character movement - turret is stationary
	UCharacterMovementComponent* CMC = GetCharacterMovement();
	if (CMC)
	{
		CMC->GravityScale = 0.0f;
		CMC->MaxWalkSpeed = 0.0f;
		CMC->MaxAcceleration = 0.0f;
		CMC->bOrientRotationToMovement = false;
		CMC->bUseControllerDesiredRotation = false;
	}

	// Turrets are immune to knockback
	KnockbackDistanceMultiplier = 0.0f;

	// Single precise shot, no burst fire
	BurstShotCount = 1;
	BurstCooldown = 0.0f;

	// Independent firing - no coordinator
	bUseCoordinator = false;
}

// ==================== Lifecycle ====================

void ASniperTurretNPC::BeginPlay()
{
	Super::BeginPlay();

	// === DEBUG: Verify critical setup ===
	UE_LOG(LogTemp, Warning, TEXT("[SniperTurret] %s BeginPlay"), *GetName());
	UE_LOG(LogTemp, Warning, TEXT("[SniperTurret]   Controller: %s"),
		GetController() ? *GetController()->GetName() : TEXT("NONE - AI won't work!"));
	UE_LOG(LogTemp, Warning, TEXT("[SniperTurret]   Weapon: %s"),
		Weapon ? *Weapon->GetName() : TEXT("NONE - can't shoot!"));
	UE_LOG(LogTemp, Warning, TEXT("[SniperTurret]   TurretMesh: %s, SkinnedAsset: %s"),
		TurretMesh ? TEXT("OK") : TEXT("MISSING"),
		(TurretMesh && TurretMesh->GetSkinnedAsset()) ? TEXT("assigned") : TEXT("NONE - invisible!"));
	UE_LOG(LogTemp, Warning, TEXT("[SniperTurret]   YawBone: '%s', PitchBone: '%s'"),
		*YawBoneName.ToString(), *PitchBoneName.ToString());
	UE_LOG(LogTemp, Warning, TEXT("[SniperTurret]   WeaponSocket: '%s'"), *TurretWeaponSocket.ToString());
}

void ASniperTurretNPC::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Turret immunity: instantly revert externally-applied stun/capture states
	// ApplyExplosionStun is non-virtual and sets these directly — we undo it each frame
	if (bStunnedByExplosion)
	{
		bStunnedByExplosion = false;
		bIsInKnockback = false;
	}
	if (bIsCaptured)
	{
		bIsCaptured = false;
		bIsInKnockback = false;
	}

	// Rotate turret toward target when engaged
	if (AimTarget.IsValid() && CurrentAimState != ETurretAimState::Idle)
	{
		UpdateTurretRotation(DeltaTime);
	}

	// Advance aim progress
	if (CurrentAimState == ETurretAimState::Aiming)
	{
		UpdateAimProgress(DeltaTime);
	}
}

// ==================== Aiming Interface ====================

void ASniperTurretNPC::StartAiming(AActor* Target)
{
	if (!Target || bIsDead)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SniperTurret] StartAiming REJECTED: Target=%s, bIsDead=%d"),
			Target ? *Target->GetName() : TEXT("null"), bIsDead);
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[SniperTurret] StartAiming at %s (prevState=%d, prevProgress=%.3f, bHasLOS=%d)"),
		*Target->GetName(), (int32)CurrentAimState, AimProgress, bHasLOS);
	AimTarget = Target;
	ResetAimProgress();
	SetAimState(ETurretAimState::Aiming);
}

void ASniperTurretNPC::StopAiming()
{
	AimTarget = nullptr;
	ResetAimProgress();
	SetAimState(ETurretAimState::Idle);

	GetWorld()->GetTimerManager().ClearTimer(DamageRecoveryTimer);
	GetWorld()->GetTimerManager().ClearTimer(PostFireCooldownTimer);
}

void ASniperTurretNPC::SetLOSStatus(bool bNewHasLOS)
{
	const bool bWasLOS = bHasLOS;
	bHasLOS = bNewHasLOS;

	if (bWasLOS != bNewHasLOS)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SniperTurret] LOS changed: %d → %d (State: %d, AimProgress=%.3f)"),
			bWasLOS, bNewHasLOS, (int32)CurrentAimState, AimProgress);
	}

	if (bWasLOS && !bNewHasLOS)
	{
		// LOS lost - reset aim progress, no recovery delay
		if (CurrentAimState == ETurretAimState::Aiming)
		{
			ResetAimProgress();
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[SniperTurret] LOS lost but state=%d (not Aiming) — AimProgress NOT reset (%.3f)"),
				(int32)CurrentAimState, AimProgress);
		}
	}

	if (!bWasLOS && bNewHasLOS)
	{
		// LOS regained — AimProgress should be 0 if properly reset
		UE_LOG(LogTemp, Warning, TEXT("[SniperTurret] LOS REGAINED: AimProgress=%.3f, State=%d %s"),
			AimProgress, (int32)CurrentAimState,
			AimProgress > 0.01f ? TEXT("*** NON-ZERO PROGRESS! ***") : TEXT("(OK)"));
	}
}

// ==================== Internal Aiming Logic ====================

void ASniperTurretNPC::UpdateAimProgress(float DeltaTime)
{
	if (!AimTarget.IsValid() || bIsDead || !bHasLOS)
	{
		return;
	}

	const float OldProgress = AimProgress;
	AimProgress += DeltaTime / AimDuration;
	AimProgress = FMath::Clamp(AimProgress, 0.0f, 1.0f);

	// Log first frame of accumulation — catches cases where progress starts non-zero
	if (OldProgress < 0.01f && AimProgress >= 0.01f)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SniperTurret] AimProgress STARTED: %.3f → %.3f (DT=%.4f, AimDuration=%.2f)"),
			OldProgress, AimProgress, DeltaTime, AimDuration);
	}

	// Log when halfway — for timing reference
	if (OldProgress < 0.5f && AimProgress >= 0.5f)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SniperTurret] AimProgress HALFWAY: %.3f (Time=%.2f)"),
			AimProgress, GetWorld()->GetTimeSeconds());
	}

	OnAimProgressChanged.Broadcast(AimProgress, CurrentAimState);

	if (AimProgress >= 1.0f)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SniperTurret] AimProgress FULL — about to fire (Time=%.2f)"),
			GetWorld()->GetTimeSeconds());
		FireAtTarget();
	}
}

// Helper: compute bone's reference-pose rotation in component space
// by walking the skeleton hierarchy from bone to root
static FQuat GetBoneRefRotCS(const UPoseableMeshComponent* Mesh, FName BoneName)
{
	if (!Mesh) return FQuat::Identity;

	const USkeletalMesh* SkelMesh = Cast<USkeletalMesh>(Mesh->GetSkinnedAsset());
	if (!SkelMesh) return FQuat::Identity;

	const FReferenceSkeleton& RefSkel = SkelMesh->GetRefSkeleton();
	const int32 BoneIdx = RefSkel.FindBoneIndex(BoneName);
	if (BoneIdx == INDEX_NONE) return FQuat::Identity;

	const TArray<FTransform>& RefPose = RefSkel.GetRefBonePose();

	// CS_bone = Local_bone * Local_parent * ... * Local_root
	FTransform CS = RefPose[BoneIdx];
	int32 ParentIdx = RefSkel.GetParentIndex(BoneIdx);
	while (ParentIdx != INDEX_NONE)
	{
		CS *= RefPose[ParentIdx];
		ParentIdx = RefSkel.GetParentIndex(ParentIdx);
	}

	return CS.GetRotation();
}

void ASniperTurretNPC::UpdateTurretRotation(float DeltaTime)
{
	if (!TurretMesh || !AimTarget.IsValid())
	{
		return;
	}

	const FVector TurretLoc = TurretMesh->GetComponentLocation();
	const FVector TargetLoc = AimTarget->GetActorLocation();
	const FRotator DesiredWorldRot = (TargetLoc - TurretLoc).Rotation();

	// Convert to rotation relative to actor (component space)
	const FRotator RelativeRot = (DesiredWorldRot - GetActorRotation()).GetNormalized();

	const float DesiredYaw = RelativeRot.Yaw;
	const float DesiredPitch = FMath::Clamp(RelativeRot.Pitch, -MaxPitchDown, MaxPitchUp);

	// Interpolate toward desired angles
	CurrentYaw = FMath::FInterpConstantTo(CurrentYaw, DesiredYaw, DeltaTime, TurretRotationSpeed);
	CurrentPitch = FMath::FInterpConstantTo(CurrentPitch, DesiredPitch, DeltaTime, TurretRotationSpeed);

	// Apply to yaw bone: compose aim offset ON TOP of reference pose
	if (YawBoneName != NAME_None)
	{
		const FQuat RestRot = GetBoneRefRotCS(TurretMesh, YawBoneName);
		const FQuat YawOffset = FQuat(FRotator(0.0f, -CurrentYaw, 0.0f));
		TurretMesh->SetBoneRotationByName(YawBoneName,
			(YawOffset * RestRot).Rotator(), EBoneSpaces::ComponentSpace);
	}

	// Apply to pitch bone: compose aim offset (yaw + pitch on roll axis) on top of reference pose
	if (PitchBoneName != NAME_None)
	{
		const FQuat RestRot = GetBoneRefRotCS(TurretMesh, PitchBoneName);
		const FQuat AimOffset = FQuat(FRotator(0.0f, -CurrentYaw, -CurrentPitch));
		TurretMesh->SetBoneRotationByName(PitchBoneName,
			(AimOffset * RestRot).Rotator(), EBoneSpaces::ComponentSpace);
	}
}

void ASniperTurretNPC::FireAtTarget()
{
	if (!AimTarget.IsValid() || !Weapon || bIsDead)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SniperTurret] FireAtTarget REJECTED: Target=%d, Weapon=%d, Dead=%d"),
			AimTarget.IsValid(), Weapon != nullptr, bIsDead);
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[Turret:%s] === FIRING at %s === (T=%.2f)"), *GetName(), *AimTarget->GetName(), GetWorld()->GetTimeSeconds());
	UE_LOG(LogTemp, Warning, TEXT("[Turret:%s]   Pre-fire: bIsShooting=%d, bWantsToShoot=%d, bInBurstCooldown=%d, bIsInKnockback=%d, CurrentBurstShots=%d"),
		*GetName(), bIsShooting, bWantsToShoot, bInBurstCooldown, bIsInKnockback, CurrentBurstShots);
	UE_LOG(LogTemp, Warning, TEXT("[SniperTurret]   Weapon: %s"),
		Weapon ? *Weapon->GetName() : TEXT("NULL"));

	SetAimState(ETurretAimState::Firing);

	// Set aim target on parent for GetWeaponTargetLocation
	CurrentAimTarget = AimTarget;

	// Fire using parent's weapon system — shot happens synchronously inside StartShooting
	StartShooting(AimTarget.Get(), true);
	// Immediately clean up: shot already fired, stop weapon and reset burst state
	// (BurstCooldown=0 causes timer to never fire, leaving bInBurstCooldown stuck true)
	StopShooting();
	bInBurstCooldown = false;
	CurrentBurstShots = 0;

	UE_LOG(LogTemp, Warning, TEXT("[SniperTurret]   Post-fire cleanup: bIsShooting=%d, bInBurstCooldown=%d"),
		bIsShooting, bInBurstCooldown);

	OnTurretFired.Broadcast();

	// Transition to post-fire cooldown
	ResetAimProgress();
	SetAimState(ETurretAimState::PostFireCooldown);

	GetWorld()->GetTimerManager().SetTimer(
		PostFireCooldownTimer,
		this, &ASniperTurretNPC::OnPostFireCooldownEnd,
		PostFireCooldownDuration, false);
}

void ASniperTurretNPC::OnDamageRecoveryEnd()
{
	UE_LOG(LogTemp, Warning, TEXT("[SniperTurret] DamageRecoveryEnd: AimProgress=%.3f, Target=%d, LOS=%d"),
		AimProgress, AimTarget.IsValid(), bHasLOS);

	if (AimTarget.IsValid() && bHasLOS && !bIsDead)
	{
		// Safety: ensure progress is 0 when re-entering Aiming
		if (AimProgress > 0.01f)
		{
			UE_LOG(LogTemp, Error, TEXT("[SniperTurret] *** BUG: AimProgress=%.3f when entering Aiming from DamageRecovery! Resetting. ***"),
				AimProgress);
			ResetAimProgress();
		}
		SetAimState(ETurretAimState::Aiming);
	}
	else
	{
		SetAimState(ETurretAimState::Idle);
	}
}

void ASniperTurretNPC::OnPostFireCooldownEnd()
{
	UE_LOG(LogTemp, Warning, TEXT("[SniperTurret] PostFireCooldownEnd: AimProgress=%.3f, Target=%d, LOS=%d"),
		AimProgress, AimTarget.IsValid(), bHasLOS);

	if (AimTarget.IsValid() && bHasLOS && !bIsDead)
	{
		// Safety: ensure progress is 0 when re-entering Aiming
		if (AimProgress > 0.01f)
		{
			UE_LOG(LogTemp, Error, TEXT("[SniperTurret] *** BUG: AimProgress=%.3f when entering Aiming from PostFireCooldown! Resetting. ***"),
				AimProgress);
			ResetAimProgress();
		}
		SetAimState(ETurretAimState::Aiming);
	}
	else
	{
		SetAimState(ETurretAimState::Idle);
	}
}

void ASniperTurretNPC::ResetAimProgress()
{
	if (AimProgress > 0.01f)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SniperTurret] ResetAimProgress: %.3f → 0.0 (State=%d)"),
			AimProgress, (int32)CurrentAimState);
	}
	AimProgress = 0.0f;
	OnAimProgressChanged.Broadcast(AimProgress, CurrentAimState);
}

void ASniperTurretNPC::SetAimState(ETurretAimState NewState)
{
	if (CurrentAimState != NewState)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Turret:%s] State: %d → %d (T=%.2f)"), *GetName(), (int32)CurrentAimState, (int32)NewState, GetWorld()->GetTimeSeconds());
	}
	CurrentAimState = NewState;
	OnAimProgressChanged.Broadcast(AimProgress, CurrentAimState);
}

// ==================== Overrides from AShooterNPC ====================

float ASniperTurretNPC::TakeDamage(float Damage, FDamageEvent const& DamageEvent,
	AController* EventInstigator, AActor* DamageCauser)
{
	// Melee damage and charge transfer pass through normally
	// Knockback is already blocked via ApplyKnockback/ApplyKnockbackVelocity overrides
	const float ActualDamage = Super::TakeDamage(Damage, DamageEvent, EventInstigator, DamageCauser);

	// CRITICAL: Parent's TakeDamage starts retaliation shooting when attacked
	// (sets bWantsToShoot=true, bIsShooting=true, defers TryStartShooting to next tick).
	// Turret must ONLY fire through its own aim progress system.
	// Reset parent's shooting state so the deferred TryStartShooting exits early.
	bIsShooting = false;
	bWantsToShoot = false;

	if (bIsDead)
	{
		return ActualDamage;
	}

	// Interrupt aiming if damage exceeds threshold
	if (ActualDamage >= AimInterruptDamageThreshold
		&& CurrentAimState == ETurretAimState::Aiming)
	{
		ResetAimProgress();
		SetAimState(ETurretAimState::DamageRecovery);

		GetWorld()->GetTimerManager().ClearTimer(DamageRecoveryTimer);
		GetWorld()->GetTimerManager().SetTimer(
			DamageRecoveryTimer,
			this, &ASniperTurretNPC::OnDamageRecoveryEnd,
			DamageRecoveryDelay, false);
	}

	return ActualDamage;
}

void ASniperTurretNPC::AttachWeaponMeshes(AShooterWeapon* WeaponToAttach)
{
	if (!WeaponToAttach)
	{
		return;
	}

	const FAttachmentTransformRules AttachRule(EAttachmentRule::SnapToTarget, false);
	WeaponToAttach->AttachToActor(this, AttachRule);

	// Hide first person mesh
	if (WeaponToAttach->GetFirstPersonMesh())
	{
		WeaponToAttach->GetFirstPersonMesh()->SetVisibility(false);
	}

	// Attach third person mesh to turret's weapon socket
	if (WeaponToAttach->GetThirdPersonMesh())
	{
		WeaponToAttach->GetThirdPersonMesh()->AttachToComponent(
			TurretMesh, AttachRule, TurretWeaponSocket);
	}
}

FVector ASniperTurretNPC::GetWeaponTargetLocation()
{
	// Sniper turret aims directly at target - no spread.
	// Progressive aim time IS the accuracy mechanic.
	if (AimTarget.IsValid())
	{
		return AimTarget->GetActorLocation();
	}

	// Fallback: aim forward
	return GetActorLocation() + GetActorForwardVector() * AimRange;
}

void ASniperTurretNPC::ApplyKnockback(const FVector& /*KnockbackDirection*/, float /*Distance*/,
	float /*Duration*/, const FVector& /*AttackerLocation*/, bool /*bKeepEMFEnabled*/)
{
	// Turrets are stationary - ignore knockback entirely
}

void ASniperTurretNPC::ApplyKnockbackVelocity(const FVector& KnockbackVelocity, float StunDuration)
{
	// Turrets are stationary - ignore knockback entirely
}

bool ASniperTurretNPC::HasLineOfSightTo(AActor* Target) const
{
	if (!Target || !GetWorld())
	{
		return false;
	}

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	FHitResult Hit;
	const FVector Start = TurretMesh
		? TurretMesh->GetComponentLocation()
		: GetActorLocation();
	const FVector End = Target->GetActorLocation();

	const bool bHit = GetWorld()->LineTraceSingleByChannel(
		Hit, Start, End, ECC_Visibility, QueryParams);

	if (bHit)
	{
		return Hit.GetActor() == Target;
	}

	return true;
}
