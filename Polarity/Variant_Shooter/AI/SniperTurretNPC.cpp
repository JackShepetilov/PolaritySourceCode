// SniperTurretNPC.cpp

#include "SniperTurretNPC.h"
#include "ShooterWeapon.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/SkeletalMesh.h"
#include "EMFVelocityModifier.h"
#include "Curves/CurveFloat.h"

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

	// Independent firing - no combat coordinator gating
	bUseCoordinator = false;

	// Sentry drives its weapon directly (StartFiring/StopFiring); it does NOT use the NPC
	// burst/permission machinery. PerceptionDelay would defer StartShooting, but we bypass
	// that path entirely, so zero it for clarity.
	PerceptionDelay = 0.0f;
}

// ==================== Lifecycle ====================

void ASniperTurretNPC::BeginPlay()
{
	Super::BeginPlay();

	// Turret must never REACT to external EM fields (it slides when charged otherwise),
	// but stays a field SOURCE: charge transfer (melee/ionization/props) keeps working.
	// Zero the per-source multipliers instead of SetEnabled(false) — EndExplosionStun
	// and knockback-end paths call SetEnabled(true) and would silently re-enable forces.
	// Done in BeginPlay (not the constructor) so BP-serialized values can't override it.
	if (EMFVelocityModifier)
	{
		EMFVelocityModifier->NPCForceMultiplier = 0.0f;
		EMFVelocityModifier->PlayerForceMultiplier = 0.0f;
		EMFVelocityModifier->ProjectileForceMultiplier = 0.0f;
		EMFVelocityModifier->EnvironmentForceMultiplier = 0.0f;
		EMFVelocityModifier->PhysicsPropForceMultiplier = 0.0f;
		EMFVelocityModifier->UnknownForceMultiplier = 0.0f;

		// Launched set replaces the normal set if launched filtering ever activates
		EMFVelocityModifier->LaunchedNPCForceMultiplier = 0.0f;
		EMFVelocityModifier->LaunchedPlayerForceMultiplier = 0.0f;
		EMFVelocityModifier->LaunchedProjectileForceMultiplier = 0.0f;
		EMFVelocityModifier->LaunchedEnvironmentForceMultiplier = 0.0f;
		EMFVelocityModifier->LaunchedPhysicsPropForceMultiplier = 0.0f;
		EMFVelocityModifier->LaunchedUnknownForceMultiplier = 0.0f;

		// Capture pull bypasses the multipliers — make sure it can't engage
		EMFVelocityModifier->bEnableViscousCapture = false;
	}

	// === DEBUG: Verify critical setup ===
	UE_LOG(LogTemp, Warning, TEXT("[SniperTurret] %s BeginPlay"), *GetName());
	UE_LOG(LogTemp, Warning, TEXT("[SniperTurret]   Controller: %s"),
		GetController() ? *GetController()->GetName() : TEXT("NONE - AI won't work!"));
	UE_LOG(LogTemp, Warning, TEXT("[SniperTurret]   Weapon: %s%s"),
		Weapon ? *Weapon->GetName() : TEXT("NONE - can't shoot!"),
		(Weapon && !Weapon->IsFullAuto()) ? TEXT("  *** NOT FULL-AUTO: sentry will only fire once! ***") : TEXT(""));
	if (Weapon && Weapon->IsHeatSystemEnabled())
	{
		UE_LOG(LogTemp, Warning, TEXT("[SniperTurret]   *** Weapon heat system is ENABLED — it slows sustained fire and fights the spin-up. Set bUseHeatSystem=false on the turret weapon. ***"));
	}
	UE_LOG(LogTemp, Warning, TEXT("[SniperTurret]   TurretMesh: %s, SkinnedAsset: %s"),
		TurretMesh ? TEXT("OK") : TEXT("MISSING"),
		(TurretMesh && TurretMesh->GetSkinnedAsset()) ? TEXT("assigned") : TEXT("NONE - invisible!"));
	UE_LOG(LogTemp, Warning, TEXT("[SniperTurret]   YawBone: '%s', PitchBone: '%s', WeaponSocket: '%s'"),
		*YawBoneName.ToString(), *PitchBoneName.ToString(), *TurretWeaponSocket.ToString());
}

void ASniperTurretNPC::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Make sure the weapon isn't left firing if we're torn down mid-engagement
	EndSentryFire();

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

	// Hard stationary enforcement. External movers write CMC Velocity / movement mode
	// directly (e.g. LaunchIntoAir: MOVE_Falling + scripted velocity) and the CMC moves
	// us in ITS OWN tick — outside the Super::Tick window measured above, so the drift
	// revert never sees it. With GravityScale=0 a single MOVE_Falling frame = hovering
	// forever. Kill both every frame while alive.
	if (!bIsDead)
	{
		if (UCharacterMovementComponent* CMC = GetCharacterMovement())
		{
			if (CMC->MovementMode != MOVE_Walking)
			{
				CMC->SetMovementMode(MOVE_Walking);
			}
			if (!CMC->Velocity.IsNearlyZero())
			{
				UE_LOG(LogTemp, Warning, TEXT("[TURRET MOVE] %s zeroing external velocity %s (mode=%d)"),
					*GetName(), *CMC->Velocity.ToCompactString(), (int32)CMC->MovementMode.GetValue());
				CMC->Velocity = FVector::ZeroVector;
			}
		}
	}

	// === Sentry engagement: track target, open/hold fire while LOS + aligned ===
	if (!bIsDead && AimTarget.IsValid())
	{
		// Slew the barrel onto the target (also refreshes bBarrelAligned)
		UpdateTurretRotation(DeltaTime);

		// Keep the parent's aim target synced so GetWeaponTargetLocation() (accuracy spread)
		// resolves against our current target.
		CurrentAimTarget = AimTarget;

		if (bHasLOS && bBarrelAligned)
		{
			BeginSentryFire();
			SetAimState(ETurretAimState::Firing);
		}
		else
		{
			EndSentryFire();
			SetAimState(ETurretAimState::Aiming);
		}
	}
	else
	{
		EndSentryFire();
		SetAimState(ETurretAimState::Idle);
	}

	// Ramp the fire rate up while firing / decay it while not (drives Weapon->ExternalFireRateMultiplier)
	if (!bIsDead)
	{
		UpdateSpinUp(DeltaTime);
	}
}

// ==================== Engagement Interface ====================

void ASniperTurretNPC::StartAiming(AActor* Target)
{
	if (!Target || bIsDead)
	{
		return;
	}

	// Target switch resets the spin-up. Same target re-acquired keeps its progress (SpinUpTarget
	// is NOT cleared on disengage), so brief drops decay via spin-down rather than hard-resetting.
	if (SpinUpTarget.Get() != Target)
	{
		SpinUpAlpha = 0.0f;
		SpinUpTarget = Target;
	}

	AimTarget = Target;
	bBarrelAligned = false;
	SetAimState(ETurretAimState::Aiming);
}

void ASniperTurretNPC::StopAiming()
{
	EndSentryFire();
	AimTarget = nullptr;
	CurrentAimTarget = nullptr;
	bBarrelAligned = false;
	SetAimState(ETurretAimState::Idle);
}

void ASniperTurretNPC::SetLOSStatus(bool bNewHasLOS)
{
	bHasLOS = bNewHasLOS;

	// Stop firing the instant LOS breaks (Tick would also catch it next frame, but this is snappier)
	if (!bHasLOS)
	{
		EndSentryFire();
	}
}

// ==================== Internal Logic ====================

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
		bBarrelAligned = false;
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

	// Barrel is "aligned" (fire gate) when the residual error on both axes is within tolerance
	const float YawError = FMath::Abs(FRotator::NormalizeAxis(DesiredYaw - CurrentYaw));
	const float PitchError = FMath::Abs(FRotator::NormalizeAxis(DesiredPitch - CurrentPitch));
	bBarrelAligned = (YawError <= FireAlignmentAngle && PitchError <= FireAlignmentAngle);

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

void ASniperTurretNPC::UpdateSpinUp(float DeltaTime)
{
	if (!Weapon)
	{
		return;
	}

	// Skip work entirely when fully cold and not firing — nothing to ramp or apply.
	if (!bSentryFiring && SpinUpAlpha <= 0.0f)
	{
		return;
	}

	if (bSentryFiring)
	{
		// Ramp toward full speed over SpinUpDuration of continuous fire
		SpinUpAlpha = (SpinUpDuration > 0.0f)
			? FMath::Min(1.0f, SpinUpAlpha + DeltaTime / SpinUpDuration)
			: 1.0f;
	}
	else
	{
		// Decay back toward cold over SpinDownDuration while not firing
		SpinUpAlpha = (SpinDownDuration > 0.0f)
			? FMath::Max(0.0f, SpinUpAlpha - DeltaTime / SpinDownDuration)
			: 0.0f;
	}

	ApplySpinUpMultiplier();
}

void ASniperTurretNPC::ApplySpinUpMultiplier()
{
	if (!Weapon)
	{
		return;
	}

	// Shape the alpha (optional curve) and map to the refire-interval multiplier.
	// Alpha 0 -> SpinUpStartMultiplier (slow), alpha 1 -> 1.0 (full authored RefireRate).
	const float ShapedAlpha = SpinUpCurve
		? FMath::Clamp(SpinUpCurve->GetFloatValue(SpinUpAlpha), 0.0f, 1.0f)
		: SpinUpAlpha;
	const float Multiplier = FMath::Lerp(SpinUpStartMultiplier, 1.0f, ShapedAlpha);

	Weapon->SetExternalFireRateMultiplier(Multiplier);
}

void ASniperTurretNPC::BeginSentryFire()
{
	if (bSentryFiring || !Weapon || bIsDead || !AimTarget.IsValid())
	{
		return;
	}

	bSentryFiring = true;
	CurrentAimTarget = AimTarget;

	// Push the current spin-up multiplier BEFORE the first shot so even shot #1's scheduled
	// cadence reflects the cold/decayed rate (StartFiring fires immediately and schedules the
	// next shot using the multiplier in effect at that instant).
	ApplySpinUpMultiplier();

	// Drive the weapon directly. With a full-auto weapon, StartFiring keeps re-firing at the
	// weapon's RefireRate until StopFiring — continuous sentry fire. (A non-full-auto weapon
	// fires a single shot; BeginPlay logs a warning in that case.)
	Weapon->StartFiring();
}

void ASniperTurretNPC::EndSentryFire()
{
	if (!bSentryFiring)
	{
		return;
	}

	bSentryFiring = false;

	if (Weapon)
	{
		Weapon->StopFiring();
	}

	// Keep the inherited NPC shooting flags clean — we never use the burst/permission path,
	// and parent::TakeDamage / TryStartShooting consult these.
	bIsShooting = false;
	bWantsToShoot = false;
}

void ASniperTurretNPC::SetAimState(ETurretAimState NewState)
{
	if (CurrentAimState != NewState)
	{
		CurrentAimState = NewState;
		OnAimProgressChanged.Broadcast(GetAimProgress(), CurrentAimState);
	}
}

// ==================== Overrides from AShooterNPC ====================

float ASniperTurretNPC::TakeDamage(float Damage, FDamageEvent const& DamageEvent,
	AController* EventInstigator, AActor* DamageCauser)
{
	// Melee damage and charge transfer pass through normally.
	// Knockback is already blocked via the ApplyKnockback/ApplyKnockbackVelocity overrides.
	const float ActualDamage = Super::TakeDamage(Damage, DamageEvent, EventInstigator, DamageCauser);

	// The sentry fires ONLY through its own engagement logic. Parent::TakeDamage runs a
	// retaliation path that can acquire the attacker as a target and schedule a shot —
	// neutralize it so a hit can't make the turret fire at something outside its engagement.
	// (Damage no longer interrupts aiming — sentries shoot through incoming fire.)
	bWantsToShoot = false;
	bIsShooting = false;

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

void ASniperTurretNPC::ApplyKnockback(const FVector& /*KnockbackDirection*/, float /*Distance*/,
	float /*Duration*/, const FVector& /*AttackerLocation*/, bool /*bKeepEMFEnabled*/, EKnockbackStyle /*Style*/)
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
