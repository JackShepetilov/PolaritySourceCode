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
#include "NiagaraSystem.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "ShooterCharacter.h"

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

	// Turret has its own progressive aim system (AimDuration) — perception delay
	// would cause StartShooting→TryStartShooting to defer the shot, but StopShooting
	// is called immediately after, killing the deferred retry → shot never fires
	PerceptionDelay = 0.0f;
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

	// Drive aim telegraph (laser VFX + player post-process) off our own progress broadcasts
	OnAimProgressChanged.AddDynamic(this, &ASniperTurretNPC::HandleAimProgressChanged);
}

void ASniperTurretNPC::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	OnAimProgressChanged.RemoveDynamic(this, &ASniperTurretNPC::HandleAimProgressChanged);

	if (ActiveAimLaser)
	{
		ActiveAimLaser->DestroyComponent();
		ActiveAimLaser = nullptr;
	}

	// Disengage the last telegraphed player so its PP state clears when the turret is destroyed mid-aim
	if (LastTelegraphedPlayer.IsValid())
	{
		if (AShooterCharacter* Shooter = Cast<AShooterCharacter>(LastTelegraphedPlayer.Get()))
		{
			Shooter->NotifyTurretTargeting(this, 0.0f, false);
		}
		LastTelegraphedPlayer = nullptr;
	}

	Super::EndPlay(EndPlayReason);
}

void ASniperTurretNPC::Tick(float DeltaTime)
{
	// === Capture pre-tick position to detect and revert unwanted movement ===
	const FVector PreTickPos = GetActorLocation();

	Super::Tick(DeltaTime);

	// === Detect & revert movement (turrets must be stationary) ===
	const FVector PostParentTickPos = GetActorLocation();
	const float Drift = FVector::Dist(PreTickPos, PostParentTickPos);
	if (Drift > 0.1f)
	{
		UE_LOG(LogTemp, Warning, TEXT("[TURRET MOVE] %s drifted %.1f! Launched=%d Captured=%d Knockback=%d KBInterp=%d Stun=%d Vel=%s Mode=%d"),
			*GetName(), Drift,
			bIsLaunched, bIsCaptured, bIsInKnockback, bIsKnockbackInterpolating, bStunnedByExplosion,
			*GetVelocity().ToCompactString(),
			GetCharacterMovement() ? (int32)GetCharacterMovement()->MovementMode.GetValue() : -1);

		// Force position back
		SetActorLocation(PreTickPos);
		if (UCharacterMovementComponent* CMC = GetCharacterMovement())
		{
			CMC->Velocity = FVector::ZeroVector;
			if (CMC->MovementMode != MOVE_Walking)
			{
				CMC->SetMovementMode(MOVE_Walking);
			}
		}
	}

	// Turret immunity: instantly revert externally-applied stun/capture/launch states
	// These non-virtual functions set flags directly — we undo them each frame
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
	if (bIsLaunched)
	{
		bIsLaunched = false;
		bIsInKnockback = false;
	}
	if (bIsKnockbackInterpolating)
	{
		bIsKnockbackInterpolating = false;
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

		// If turret went Idle because LOS was lost during PostFireCooldown/DamageRecovery,
		// restart aiming now that LOS is back
		if (CurrentAimState == ETurretAimState::Idle && AimTarget.IsValid() && !bIsDead)
		{
			UE_LOG(LogTemp, Warning, TEXT("[SniperTurret] Restarting aim from Idle (LOS regained)"));
			ResetAimProgress();
			SetAimState(ETurretAimState::Aiming);
		}
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
		UE_LOG(LogTemp, Error, TEXT("[SniperTurret] FireAtTarget REJECTED: Target=%d, Weapon=%d, Dead=%d"),
			AimTarget.IsValid(), Weapon != nullptr, bIsDead);
		return;
	}

	const float Now = GetWorld()->GetTimeSeconds();
	UE_LOG(LogTemp, Error, TEXT("[Turret:%s] ========== FIRE ATTEMPT at %s ========== (T=%.2f)"),
		*GetName(), *AimTarget->GetName(), Now);
	UE_LOG(LogTemp, Error, TEXT("[Turret:%s]   NPC state: bIsShooting=%d, bWantsToShoot=%d, bInBurstCooldown=%d, bIsInKnockback=%d, bIsCaptured=%d, bStunnedByExplosion=%d, CurrentBurstShots=%d"),
		*GetName(), bIsShooting, bWantsToShoot, bInBurstCooldown, bIsInKnockback, bIsCaptured, bStunnedByExplosion, CurrentBurstShots);
	UE_LOG(LogTemp, Error, TEXT("[Turret:%s]   PerceptionDelay=%.3f, TargetAcquiredTime=%.2f, PerceptionTarget=%s"),
		*GetName(), PerceptionDelay, TargetAcquiredTime,
		PerceptionDelayTrackedTarget.IsValid() ? *PerceptionDelayTrackedTarget->GetName() : TEXT("null"));
	UE_LOG(LogTemp, Error, TEXT("[Turret:%s]   Weapon: %s, RefireRate=%.3f, IsHitscan=%d"),
		*GetName(), *Weapon->GetName(), Weapon->GetActualRefireRate(), Weapon->IsHitscan());

	SetAimState(ETurretAimState::Firing);

	// Set aim target on parent for GetWeaponTargetLocation
	CurrentAimTarget = AimTarget;

	// === BYPASS parent's StartShooting entirely — fire weapon directly ===
	// StartShooting → TryStartShooting has too many potential blockers
	// (PerceptionDelay, bInBurstCooldown, permission retry, etc.)
	// Turret's own aim system already handles all timing.
	UE_LOG(LogTemp, Error, TEXT("[Turret:%s]   Calling Weapon->StartFiring()..."), *GetName());
	Weapon->StartFiring();
	UE_LOG(LogTemp, Error, TEXT("[Turret:%s]   StartFiring returned. Calling StopFiring..."), *GetName());
	Weapon->StopFiring();

	// Reset NPC shooting state (don't leave stale flags from OnWeaponShotFired callback)
	bIsShooting = false;
	bWantsToShoot = false;
	bInBurstCooldown = false;
	CurrentBurstShots = 0;

	UE_LOG(LogTemp, Error, TEXT("[Turret:%s]   Post-fire cleanup done."), *GetName());

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

// ==================== Aim Telegraph ====================

void ASniperTurretNPC::HandleAimProgressChanged(float Progress, ETurretAimState AimState)
{
	const bool bIsActivelyAiming = (AimState == ETurretAimState::Aiming);

	// ----- Laser VFX lifecycle -----
	if (bIsActivelyAiming)
	{
		// Spawn on first entry; reactivate if it was previously deactivated
		if (!ActiveAimLaser)
		{
			if (AimLaserVFX && TurretMesh)
			{
				ActiveAimLaser = UNiagaraFunctionLibrary::SpawnSystemAttached(
					AimLaserVFX,
					TurretMesh,
					TurretWeaponSocket,
					AimLaserSpawnOffset,
					FRotator::ZeroRotator,
					EAttachLocation::KeepRelativeOffset,
					false  // bAutoDestroy=false — we control the lifecycle
				);
			}
		}
		else if (!ActiveAimLaser->IsActive())
		{
			ActiveAimLaser->Activate(true);
		}

		// Feed aim progress + beam endpoint into the Niagara system
		if (ActiveAimLaser)
		{
			UNiagaraFunctionLibrary::SetNiagaraVariableFloat(
				ActiveAimLaser, AimLaserIntensityParam.ToString(), Progress);

			if (AimTarget.IsValid())
			{
				FVector EndLoc = AimTarget->GetActorLocation();
				if (const ACharacter* CharTarget = Cast<ACharacter>(AimTarget.Get()))
				{
					if (const UCapsuleComponent* Capsule = CharTarget->GetCapsuleComponent())
					{
						EndLoc.Z += Capsule->GetScaledCapsuleHalfHeight();
					}
				}
				UNiagaraFunctionLibrary::SetNiagaraVariableVec3(
					ActiveAimLaser, AimLaserBeamEndParam.ToString(), EndLoc);
			}
		}
	}
	else
	{
		// Deactivate (not Destroy) so Niagara can fade out via its own Lifetime Energy
		if (ActiveAimLaser && ActiveAimLaser->IsActive())
		{
			ActiveAimLaser->Deactivate();
		}
	}

	// ----- Player-side post-process notification -----
	AActor* CurrentPlayer = (bIsActivelyAiming && AimTarget.IsValid()) ? AimTarget.Get() : nullptr;

	// Disengage previous player if target changed or aim stopped
	if (LastTelegraphedPlayer.IsValid() && LastTelegraphedPlayer.Get() != CurrentPlayer)
	{
		if (AShooterCharacter* PrevShooter = Cast<AShooterCharacter>(LastTelegraphedPlayer.Get()))
		{
			PrevShooter->NotifyTurretTargeting(this, 0.0f, false);
		}
	}

	// Engage current player (if any)
	if (CurrentPlayer)
	{
		if (AShooterCharacter* Shooter = Cast<AShooterCharacter>(CurrentPlayer))
		{
			Shooter->NotifyTurretTargeting(this, Progress, true);
			LastTelegraphedPlayer = CurrentPlayer;
		}
		else
		{
			LastTelegraphedPlayer = nullptr;
		}
	}
	else
	{
		LastTelegraphedPlayer = nullptr;
	}
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
	// Sniper turret aims directly at target center mass - no spread.
	// Progressive aim time IS the accuracy mechanic.
	if (AimTarget.IsValid())
	{
		FVector TargetLoc = AimTarget->GetActorLocation();
		if (const ACharacter* CharTarget = Cast<ACharacter>(AimTarget.Get()))
		{
			if (const UCapsuleComponent* Capsule = CharTarget->GetCapsuleComponent())
			{
				TargetLoc.Z += Capsule->GetScaledCapsuleHalfHeight();
			}
		}
		return TargetLoc;
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

	// Start trace from muzzle socket (where the weapon actually fires from)
	FVector Start;
	if (TurretMesh && TurretMesh->DoesSocketExist(TurretWeaponSocket))
	{
		Start = TurretMesh->GetSocketLocation(TurretWeaponSocket);
	}
	else if (TurretMesh)
	{
		Start = TurretMesh->GetComponentLocation();
	}
	else
	{
		Start = GetActorLocation();
	}

	// Aim at target center mass, not feet
	FVector End = Target->GetActorLocation();
	if (const ACharacter* CharTarget = Cast<ACharacter>(Target))
	{
		if (const UCapsuleComponent* Capsule = CharTarget->GetCapsuleComponent())
		{
			End.Z += Capsule->GetScaledCapsuleHalfHeight();
		}
	}

	FHitResult Hit;
	const bool bHit = GetWorld()->LineTraceSingleByChannel(
		Hit, Start, End, ECC_Visibility, QueryParams);

	if (bHit)
	{
		return Hit.GetActor() == Target;
	}

	return true;
}
