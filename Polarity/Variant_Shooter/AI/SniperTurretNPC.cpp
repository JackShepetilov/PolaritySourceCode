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

	// Periodic state dump (every 2 seconds)
	static double LastStateDump = 0;
	if (AimTarget.IsValid() && GetWorld()->GetTimeSeconds() - LastStateDump > 2.0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SniperTurret] STATE: AimState=%d, AimProgress=%.2f, LOS=%d, BurstCD=%d, bIsShooting=%d, Target=%s"),
			(int32)CurrentAimState, AimProgress, bHasLOS, bInBurstCooldown, bIsShooting,
			AimTarget.IsValid() ? *AimTarget->GetName() : TEXT("null"));
		LastStateDump = GetWorld()->GetTimeSeconds();
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

	UE_LOG(LogTemp, Log, TEXT("[SniperTurret] StartAiming at %s"), *Target->GetName());
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
		UE_LOG(LogTemp, Log, TEXT("[SniperTurret] LOS changed: %d → %d (State: %d)"),
			bWasLOS, bNewHasLOS, (int32)CurrentAimState);
	}

	if (bWasLOS && !bNewHasLOS)
	{
		// LOS lost - reset aim progress, no recovery delay
		if (CurrentAimState == ETurretAimState::Aiming)
		{
			ResetAimProgress();
		}
	}
	// When LOS regained, UpdateAimProgress resumes naturally since bHasLOS is true
}

// ==================== Internal Aiming Logic ====================

void ASniperTurretNPC::UpdateAimProgress(float DeltaTime)
{
	if (!AimTarget.IsValid() || bIsDead || !bHasLOS)
	{
		// Log why we're not progressing (throttled to once per second)
		static double LastSkipLog = 0;
		if (GetWorld()->GetTimeSeconds() - LastSkipLog > 1.0)
		{
			UE_LOG(LogTemp, Warning, TEXT("[SniperTurret] AimProgress SKIPPED: Target=%d, Dead=%d, LOS=%d, State=%d"),
				AimTarget.IsValid(), bIsDead, bHasLOS, (int32)CurrentAimState);
			LastSkipLog = GetWorld()->GetTimeSeconds();
		}
		return;
	}

	AimProgress += DeltaTime / AimDuration;
	AimProgress = FMath::Clamp(AimProgress, 0.0f, 1.0f);

	OnAimProgressChanged.Broadcast(AimProgress, CurrentAimState);

	if (AimProgress >= 1.0f)
	{
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

	UE_LOG(LogTemp, Warning, TEXT("[SniperTurret] === FIRING at %s ==="), *AimTarget->GetName());
	UE_LOG(LogTemp, Warning, TEXT("[SniperTurret]   Pre-fire: bIsShooting=%d, bWantsToShoot=%d, bInBurstCooldown=%d, bIsInKnockback=%d, CurrentBurstShots=%d"),
		bIsShooting, bWantsToShoot, bInBurstCooldown, bIsInKnockback, CurrentBurstShots);
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
	if (AimTarget.IsValid() && bHasLOS && !bIsDead)
	{
		SetAimState(ETurretAimState::Aiming);
	}
	else
	{
		SetAimState(ETurretAimState::Idle);
	}
}

void ASniperTurretNPC::OnPostFireCooldownEnd()
{
	if (AimTarget.IsValid() && bHasLOS && !bIsDead)
	{
		SetAimState(ETurretAimState::Aiming);
	}
	else
	{
		SetAimState(ETurretAimState::Idle);
	}
}

void ASniperTurretNPC::ResetAimProgress()
{
	AimProgress = 0.0f;
	OnAimProgressChanged.Broadcast(AimProgress, CurrentAimState);
}

void ASniperTurretNPC::SetAimState(ETurretAimState NewState)
{
	if (CurrentAimState != NewState)
	{
		UE_LOG(LogTemp, Log, TEXT("[SniperTurret] State: %d → %d"), (int32)CurrentAimState, (int32)NewState);
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
