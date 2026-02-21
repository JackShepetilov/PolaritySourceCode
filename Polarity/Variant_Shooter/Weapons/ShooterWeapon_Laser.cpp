// ShooterWeapon_Laser.cpp

#include "ShooterWeapon_Laser.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "EMFVelocityModifier.h"
#include "EMF_FieldComponent.h"
#include "../DamageTypes/DamageType_EMFWeapon.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/ShooterDummy.h"
#include "EMFPhysicsProp.h"

namespace
{
	/** Check if actor is dead after TakeDamage (synchronous check via HP/bIsDead flags) */
	bool IsActorDeadAfterDamage(AActor* Actor)
	{
		if (!IsValid(Actor))
		{
			return true;
		}

		if (AShooterNPC* NPC = Cast<AShooterNPC>(Actor))
		{
			return NPC->IsDead();
		}

		if (AShooterCharacter* ShooterChar = Cast<AShooterCharacter>(Actor))
		{
			return ShooterChar->IsDead();
		}

		if (AShooterDummy* Dummy = Cast<AShooterDummy>(Actor))
		{
			return Dummy->IsDead();
		}

		if (AEMFPhysicsProp* Prop = Cast<AEMFPhysicsProp>(Actor))
		{
			return Prop->IsDead();
		}

		return Actor->IsPendingKillPending();
	}
}

AShooterWeapon_Laser::AShooterWeapon_Laser()
{
	// Laser is always full-auto (hold to fire)
	bFullAuto = true;

	// Not hitscan or projectile - we handle firing ourselves
	bUseHitscan = false;

	// Default damage type
	LaserDamageType = UDamageType_EMFWeapon::StaticClass();
}

void AShooterWeapon_Laser::BeginPlay()
{
	Super::BeginPlay();
}

// =============================================================================
// Fire() override - called once when trigger is pulled
// Sets up the beam, does NOT call Super (no refire timer needed)
// =============================================================================
void AShooterWeapon_Laser::Fire()
{
	// Don't call Super::Fire() - we don't want refire timers or per-shot logic
	if (!bIsFiring)
	{
		return;
	}

	// Don't activate main beam during Second Harmonic ability
	if (CurrentHarmonicPhase != ESecondHarmonicPhase::None)
	{
		return;
	}

	// Activate beam on first fire
	if (!bBeamActive)
	{
		ActivateBeam();
	}

	// Record shot time (for StartFiring cooldown check)
	TimeOfLastShot = GetWorld()->GetTimeSeconds();
}

// =============================================================================
// Tick() - continuous beam logic while firing
// =============================================================================
void AShooterWeapon_Laser::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime); // Handles heat decay

	// Second Harmonic ability takes priority over normal beam
	if (CurrentHarmonicPhase != ESecondHarmonicPhase::None)
	{
		UpdateSecondHarmonic(DeltaTime);
		return;
	}

	// Check if we should deactivate beam (player released trigger)
	if (bBeamActive && !bIsFiring)
	{
		DeactivateBeam();
		return;
	}

	if (!bBeamActive)
	{
		return;
	}

	// --- Continuous beam logic ---

	// 1. Heat accumulation (continuous, not per-shot)
	if (bUseHeatSystem)
	{
		AddHeat(HeatPerSecond * DeltaTime);
	}

	// 2. Perform beam trace
	FHitResult HitResult;
	FVector BeamStart, BeamEnd;
	bool bHitPawn = false;
	const bool bHit = PerformBeamTrace(HitResult, BeamStart, BeamEnd, bHitPawn);

	// 3. Update beam VFX
	UpdateBeamVFX(BeamStart, BeamEnd);

	// 4. Handle hit
	if (bHit && bHitPawn && HitResult.GetActor())
	{
		// Hit a pawn/character - apply damage, ionization, show impact
		ApplyBeamDamage(HitResult, DeltaTime);
		ApplyIonization(HitResult.GetActor(), DeltaTime);
		UpdateImpactVFX(true, HitResult.ImpactPoint, HitResult.ImpactNormal);
		CurrentHitActor = HitResult.GetActor();
	}
	else if (bHit)
	{
		// Hit a surface (wall, floor) - impact VFX only, no damage/hitmarker
		UpdateImpactVFX(true, HitResult.ImpactPoint, HitResult.ImpactNormal);
		CurrentHitActor = nullptr;
	}
	else
	{
		// No hit - beam goes to max range
		UpdateImpactVFX(false, FVector::ZeroVector, FVector::UpVector);
		CurrentHitActor = nullptr;
	}
}

// =============================================================================
// PerformBeamTrace - line trace from camera, beam VFX starts from muzzle
// Two traces: ECC_Visibility for walls, ECC_Pawn for characters
// Returns the closest hit (wall or pawn)
// =============================================================================
bool AShooterWeapon_Laser::PerformBeamTrace(FHitResult& OutHit, FVector& OutBeamStart, FVector& OutBeamEnd, bool& bOutHitPawn) const
{
	bOutHitPawn = false;
	// Muzzle location for VFX start
	USkeletalMeshComponent* MuzzleMesh = (PawnOwner && PawnOwner->IsPlayerControlled()) ? GetFirstPersonMesh() : GetThirdPersonMesh();
	OutBeamStart = MuzzleMesh ? MuzzleMesh->GetSocketLocation(MuzzleSocketName) : GetActorLocation();

	// Aim from camera/view (same as GetWeaponTargetLocation)
	FVector ViewLocation = OutBeamStart;
	FVector ViewDir = FVector::ForwardVector;

	if (PawnOwner)
	{
		ViewDir = PawnOwner->GetBaseAimRotation().Vector();
		ViewLocation = PawnOwner->GetPawnViewLocation();
	}

	const FVector TraceEnd = ViewLocation + ViewDir * MaxBeamRange;

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);
	QueryParams.AddIgnoredActor(GetOwner());

	// Trace 1: Walls/geometry (ECC_Visibility)
	FHitResult WallHit;
	const bool bHitWall = GetWorld()->LineTraceSingleByChannel(
		WallHit,
		ViewLocation,
		TraceEnd,
		ECC_Visibility,
		QueryParams
	);

	// Determine max pawn trace distance (don't go past walls)
	const FVector PawnTraceEnd = bHitWall ? WallHit.ImpactPoint : TraceEnd;

	// Trace 2: Pawns + PhysicsBody (ECC_Pawn, ECC_PhysicsBody) - line trace by object type
	FHitResult PawnHit;
	FCollisionObjectQueryParams PawnObjectParams;
	PawnObjectParams.AddObjectTypesToQuery(ECC_Pawn);
	PawnObjectParams.AddObjectTypesToQuery(ECC_PhysicsBody);

	const bool bHitPawn = GetWorld()->LineTraceSingleByObjectType(
		PawnHit,
		ViewLocation,
		PawnTraceEnd,
		PawnObjectParams,
		QueryParams
	);

	// Pick the closest hit
	if (bHitPawn)
	{
		OutHit = PawnHit;
		OutBeamEnd = PawnHit.ImpactPoint;
		bOutHitPawn = true;
		return true;
	}
	else if (bHitWall)
	{
		OutHit = WallHit;
		OutBeamEnd = WallHit.ImpactPoint;
		return true;
	}

	// No hit - beam goes to max range
	OutBeamEnd = OutBeamStart + ViewDir * MaxBeamRange;
	return false;
}

// =============================================================================
// ApplyBeamDamage - DPS-based continuous damage
// =============================================================================
void AShooterWeapon_Laser::ApplyBeamDamage(const FHitResult& Hit, float DeltaTime)
{
	AActor* HitActor = Hit.GetActor();
	if (!HitActor)
	{
		return;
	}

	// Calculate damage for this frame
	float FrameDamage = DamagePerSecond * DeltaTime;

	// Apply heat damage multiplier
	if (bUseHeatSystem)
	{
		FrameDamage *= CalculateHeatDamageMultiplier();
	}

	// Apply Z-Factor
	if (bUseZFactor && PawnOwner)
	{
		FrameDamage *= CalculateZFactorMultiplier(PawnOwner->GetActorLocation().Z, HitActor->GetActorLocation().Z);
	}

	// Apply tag multipliers
	FrameDamage *= GetTagDamageMultiplier(HitActor);

	if (FrameDamage <= 0.0f)
	{
		return;
	}

	// Apply damage
	UGameplayStatics::ApplyPointDamage(
		HitActor,
		FrameDamage,
		Hit.TraceEnd - Hit.TraceStart, // Shot direction
		Hit,
		PawnOwner ? PawnOwner->GetController() : nullptr,
		this,
		LaserDamageType
	);

	// Notify weapon owner about hit (for hit markers, etc.)
	if (WeaponOwner)
	{
		const bool bKilled = IsActorDeadAfterDamage(HitActor);
		WeaponOwner->OnWeaponHit(
			Hit.ImpactPoint,
			(Hit.TraceEnd - Hit.TraceStart).GetSafeNormal(),
			FrameDamage,
			false, // No headshots for laser MVP
			bKilled
		);
	}
}

// =============================================================================
// ApplyIonization - add positive charge to target
// =============================================================================
void AShooterWeapon_Laser::ApplyIonization(AActor* Target, float DeltaTime)
{
	if (!Target)
	{
		return;
	}

	const float ChargeToAdd = IonizationChargePerSecond * DeltaTime;

	// Try UEMFVelocityModifier first (for characters/NPCs)
	if (UEMFVelocityModifier* TargetModifier = Target->FindComponentByClass<UEMFVelocityModifier>())
	{
		// Use GetCharge() to read actual FieldComponent charge (not BaseCharge which may be stale
		// after melee's SetCharge() calls that bypass BaseCharge tracking)
		const float CurrentCharge = TargetModifier->GetCharge();

		// Already at max positive charge - nothing to do
		if (CurrentCharge >= MaxIonizationCharge)
		{
			return;
		}

		// Add charge towards positive (ionization)
		// If negative: move towards 0, then towards positive
		// If positive: increase further
		const float NewCharge = FMath::Min(CurrentCharge + ChargeToAdd, MaxIonizationCharge);
		TargetModifier->SetCharge(NewCharge);
		return;
	}

	// Fallback: try raw UEMF_FieldComponent (for objects without movement modifier)
	if (UEMF_FieldComponent* TargetField = Target->FindComponentByClass<UEMF_FieldComponent>())
	{
		FEMSourceDescription Desc = TargetField->GetSourceDescription();
		const float CurrentCharge = Desc.PointChargeParams.Charge;

		if (CurrentCharge >= MaxIonizationCharge)
		{
			return;
		}

		Desc.PointChargeParams.Charge = FMath::Min(CurrentCharge + ChargeToAdd, MaxIonizationCharge);
		TargetField->SetSourceDescription(Desc);
	}
}

// =============================================================================
// ActivateBeam - spawn VFX and start audio
// =============================================================================
void AShooterWeapon_Laser::ActivateBeam()
{
	bBeamActive = true;

	// Spawn beam Niagara attached to muzzle socket
	if (LaserBeamFX)
	{
		USkeletalMeshComponent* MuzzleMesh = (PawnOwner && PawnOwner->IsPlayerControlled()) ? GetFirstPersonMesh() : GetThirdPersonMesh();

		if (MuzzleMesh)
		{
			ActiveBeamComponent = UNiagaraFunctionLibrary::SpawnSystemAttached(
				LaserBeamFX,
				MuzzleMesh,
				MuzzleSocketName,
				FVector::ZeroVector,
				FRotator::ZeroRotator,
				EAttachLocation::SnapToTarget,
				false,   // bAutoDestroy = false (we manage lifetime)
				false,   // bAutoActivate = false (we activate after setting params)
				ENCPoolMethod::None
			);
		}

		if (ActiveBeamComponent)
		{
			// Niagara must tick after us so it reads freshly-set parameters
			ActiveBeamComponent->AddTickPrerequisiteActor(this);

			ActiveBeamComponent->SetColorParameter(FName("ColorEnergy"), LaserColorEnergy);
			ActiveBeamComponent->SetFloatParameter(FName("Scale_E"), BeamScaleE);
			ActiveBeamComponent->SetVectorParameter(FName("Scale_E_Mesh"), FVector(BeamScaleEMesh));

			ActiveBeamComponent->Activate();
		}
	}

	// Spawn impact VFX attached to weapon (we reposition it each frame)
	if (LaserImpactFX)
	{
		ActiveImpactComponent = UNiagaraFunctionLibrary::SpawnSystemAttached(
			LaserImpactFX,
			RootComponent,
			NAME_None,
			FVector::ZeroVector,
			FRotator::ZeroRotator,
			EAttachLocation::KeepWorldPosition,
			false,
			false,   // bAutoActivate = false (activated when beam hits something)
			ENCPoolMethod::None
		);
	}

	// Play start sound
	if (BeamStartSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, BeamStartSound, GetActorLocation());
	}

	// Start loop sound
	if (BeamLoopSound)
	{
		BeamLoopAudioComponent = UGameplayStatics::SpawnSoundAttached(
			BeamLoopSound,
			GetFirstPersonMesh(),
			MuzzleSocketName,
			FVector::ZeroVector,
			EAttachLocation::SnapToTarget,
			true // bStopWhenAttachedToDestroyed
		);
	}
}

// =============================================================================
// DeactivateBeam - clean up VFX and audio
// =============================================================================
void AShooterWeapon_Laser::DeactivateBeam()
{
	bBeamActive = false;

	// Destroy beam VFX
	if (ActiveBeamComponent)
	{
		ActiveBeamComponent->DestroyComponent();
		ActiveBeamComponent = nullptr;
	}

	// Destroy impact VFX
	if (ActiveImpactComponent)
	{
		ActiveImpactComponent->DestroyComponent();
		ActiveImpactComponent = nullptr;
	}

	// Stop loop sound
	if (BeamLoopAudioComponent)
	{
		BeamLoopAudioComponent->Stop();
		BeamLoopAudioComponent = nullptr;
	}

	// Play stop sound
	if (BeamStopSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, BeamStopSound, GetActorLocation());
	}

	CurrentHitActor = nullptr;
}

// =============================================================================
// UpdateBeamVFX - set Niagara parameters matching the beam system from screenshot
// =============================================================================
void AShooterWeapon_Laser::UpdateBeamVFX(const FVector& Start, const FVector& End)
{
	if (!ActiveBeamComponent)
	{
		return;
	}

	const FVector Direction = (End - Start).GetSafeNormal();

	ActiveBeamComponent->SetVariablePosition(FName("Beam Start"), Start);
	ActiveBeamComponent->SetVariablePosition(FName("Beam End"), End);
	ActiveBeamComponent->SetVectorParameter(FName("Axis"), Direction);
	ActiveBeamComponent->SetColorParameter(FName("ColorEnergy"), LaserColorEnergy);
	ActiveBeamComponent->SetFloatParameter(FName("Scale_E"), BeamScaleE);
	ActiveBeamComponent->SetVectorParameter(FName("Scale_E_Mesh"), FVector(BeamScaleEMesh));
}

// =============================================================================
// UpdateImpactVFX - position impact effect at beam endpoint
// =============================================================================
void AShooterWeapon_Laser::UpdateImpactVFX(bool bHitSurface, const FVector& Location, const FVector& Normal)
{
	if (!ActiveImpactComponent)
	{
		return;
	}

	if (bHitSurface)
	{
		if (!ActiveImpactComponent->IsActive())
		{
			ActiveImpactComponent->Activate();
		}
		ActiveImpactComponent->SetWorldLocation(Location);
		ActiveImpactComponent->SetWorldRotation(Normal.Rotation());
	}
	else
	{
		if (ActiveImpactComponent->IsActive())
		{
			ActiveImpactComponent->Deactivate();
		}
	}
}

// =============================================================================
// SECOND HARMONIC GENERATION ABILITY
// =============================================================================

// =============================================================================
// OnSecondaryAction - intercept ADS button to trigger ability
// Returns true to block normal ADS behavior
// =============================================================================
bool AShooterWeapon_Laser::OnSecondaryAction()
{
	// Already running
	if (CurrentHarmonicPhase != ESecondHarmonicPhase::None)
	{
		return true;
	}

	// Cooldown check
	const float TimeSinceLastUse = GetWorld()->GetTimeSeconds() - LastHarmonicUseTime;
	if (TimeSinceLastUse < SecondHarmonicCooldown)
	{
		return true; // Still block ADS even on cooldown
	}

	ActivateSecondHarmonic();
	return true;
}

// =============================================================================
// ActivateSecondHarmonic - start the vertical sweep phase
// =============================================================================
void AShooterWeapon_Laser::ActivateSecondHarmonic()
{
	// Remember main beam state so we can restore it after
	bMainBeamWasActive = bBeamActive;

	// Deactivate main beam during ability
	if (bBeamActive)
	{
		DeactivateBeam();
	}

	// Start vertical sweep
	CurrentHarmonicPhase = ESecondHarmonicPhase::VerticalSweep;
	HarmonicPhaseElapsedTime = 0.0f;
	HitActorsBeamA.Empty();
	HitActorsBeamB.Empty();

	// Spawn the two sweep beams
	SpawnHarmonicBeams();
}

// =============================================================================
// UpdateSecondHarmonic - per-frame ability logic
// =============================================================================
void AShooterWeapon_Laser::UpdateSecondHarmonic(float DeltaTime)
{
	HarmonicPhaseElapsedTime += DeltaTime;

	// Determine current phase duration and rotation axis
	const float PhaseDuration = (CurrentHarmonicPhase == ESecondHarmonicPhase::VerticalSweep)
		? VerticalSweepDuration
		: HorizontalSweepDuration;

	const float Alpha = FMath::Clamp(HarmonicPhaseElapsedTime / PhaseDuration, 0.0f, 1.0f);
	const float CurrentAngle = InitialSweepAngleDeg * (1.0f - Alpha);

	// Get aim direction and rotation axes from owner
	if (!PawnOwner)
	{
		DeactivateSecondHarmonic();
		return;
	}

	const FRotator AimRot = PawnOwner->GetBaseAimRotation();
	const FVector AimDir = AimRot.Vector();
	const FVector RightAxis = FRotationMatrix(AimRot).GetUnitAxis(EAxis::Y);
	const FVector UpAxis = FRotationMatrix(AimRot).GetUnitAxis(EAxis::Z);

	// Choose rotation axis based on phase
	const FVector& RotationAxis = (CurrentHarmonicPhase == ESecondHarmonicPhase::VerticalSweep)
		? RightAxis   // Vertical: rotate around right axis (pitch up/down)
		: UpAxis;     // Horizontal: rotate around up axis (yaw left/right)

	// Calculate beam directions
	const FVector DirA = AimDir.RotateAngleAxis(+CurrentAngle, RotationAxis);
	const FVector DirB = AimDir.RotateAngleAxis(-CurrentAngle, RotationAxis);

	// --- Beam A ---
	{
		FHitResult HitA;
		FVector StartA, EndA;
		bool bHitPawnA = false;
		PerformSweepTrace(DirA, HitA, StartA, EndA, bHitPawnA);

		// One-time damage on new targets
		if (bHitPawnA && HitA.GetActor())
		{
			TWeakObjectPtr<AActor> HitActorWeak = HitA.GetActor();
			if (!HitActorsBeamA.Contains(HitActorWeak))
			{
				HitActorsBeamA.Add(HitActorWeak);

				// Apply one-time damage
				const TSubclassOf<UDamageType> DmgType = SecondHarmonicDamageType ? SecondHarmonicDamageType : LaserDamageType;
				UGameplayStatics::ApplyPointDamage(
					HitA.GetActor(),
					SecondHarmonicDamage,
					DirA,
					HitA,
					PawnOwner ? PawnOwner->GetController() : nullptr,
					this,
					DmgType
				);

				// Hitmarker
				if (WeaponOwner)
				{
					const bool bKilled = IsActorDeadAfterDamage(HitA.GetActor());
					WeaponOwner->OnWeaponHit(HitA.ImpactPoint, DirA, SecondHarmonicDamage, false, bKilled);
				}
			}
		}

		UpdateHarmonicBeamVFX(ActiveHarmonicBeamA, StartA, EndA);
	}

	// --- Beam B ---
	{
		FHitResult HitB;
		FVector StartB, EndB;
		bool bHitPawnB = false;
		PerformSweepTrace(DirB, HitB, StartB, EndB, bHitPawnB);

		// One-time damage on new targets
		if (bHitPawnB && HitB.GetActor())
		{
			TWeakObjectPtr<AActor> HitActorWeak = HitB.GetActor();
			if (!HitActorsBeamB.Contains(HitActorWeak))
			{
				HitActorsBeamB.Add(HitActorWeak);

				const TSubclassOf<UDamageType> DmgType = SecondHarmonicDamageType ? SecondHarmonicDamageType : LaserDamageType;
				UGameplayStatics::ApplyPointDamage(
					HitB.GetActor(),
					SecondHarmonicDamage,
					DirB,
					HitB,
					PawnOwner ? PawnOwner->GetController() : nullptr,
					this,
					DmgType
				);

				if (WeaponOwner)
				{
					const bool bKilled = IsActorDeadAfterDamage(HitB.GetActor());
					WeaponOwner->OnWeaponHit(HitB.ImpactPoint, DirB, SecondHarmonicDamage, false, bKilled);
				}
			}
		}

		UpdateHarmonicBeamVFX(ActiveHarmonicBeamB, StartB, EndB);
	}

	// Check phase completion
	if (Alpha >= 1.0f)
	{
		if (CurrentHarmonicPhase == ESecondHarmonicPhase::VerticalSweep)
		{
			TransitionToHorizontalSweep();
		}
		else
		{
			DeactivateSecondHarmonic();
		}
	}
}

// =============================================================================
// TransitionToHorizontalSweep - switch from vertical to horizontal phase
// =============================================================================
void AShooterWeapon_Laser::TransitionToHorizontalSweep()
{
	CurrentHarmonicPhase = ESecondHarmonicPhase::HorizontalSweep;
	HarmonicPhaseElapsedTime = 0.0f;

	// Fresh hit tracking for the new pair of beams
	HitActorsBeamA.Empty();
	HitActorsBeamB.Empty();

	// Beams stay alive - just change sweep direction next frame
}

// =============================================================================
// DeactivateSecondHarmonic - end ability, restore main beam if needed
// =============================================================================
void AShooterWeapon_Laser::DeactivateSecondHarmonic()
{
	CurrentHarmonicPhase = ESecondHarmonicPhase::None;
	LastHarmonicUseTime = GetWorld()->GetTimeSeconds();

	DestroyHarmonicBeams();

	HitActorsBeamA.Empty();
	HitActorsBeamB.Empty();

	// Restore main beam if it was active before and player is still holding fire
	if (bMainBeamWasActive && bIsFiring)
	{
		ActivateBeam();
	}
}

// =============================================================================
// PerformSweepTrace - line trace for a single sweep beam in a given direction
// Same two-trace approach as PerformBeamTrace (walls + pawns)
// =============================================================================
bool AShooterWeapon_Laser::PerformSweepTrace(const FVector& Direction, FHitResult& OutHit, FVector& OutBeamStart, FVector& OutBeamEnd, bool& bOutHitPawn) const
{
	bOutHitPawn = false;

	// Muzzle location for VFX start (same as main beam)
	USkeletalMeshComponent* MuzzleMesh = (PawnOwner && PawnOwner->IsPlayerControlled()) ? GetFirstPersonMesh() : GetThirdPersonMesh();
	OutBeamStart = MuzzleMesh ? MuzzleMesh->GetSocketLocation(MuzzleSocketName) : GetActorLocation();

	const FVector TraceStart = PawnOwner ? PawnOwner->GetPawnViewLocation() : OutBeamStart;
	const FVector TraceEnd = TraceStart + Direction * MaxBeamRange;

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);
	QueryParams.AddIgnoredActor(GetOwner());

	// Trace 1: Walls/geometry
	FHitResult WallHit;
	const bool bHitWall = GetWorld()->LineTraceSingleByChannel(
		WallHit, TraceStart, TraceEnd, ECC_Visibility, QueryParams
	);

	// Trace 2: Pawns + PhysicsBody (limited by wall distance)
	const FVector PawnTraceEnd = bHitWall ? WallHit.ImpactPoint : TraceEnd;
	FHitResult PawnHit;
	FCollisionObjectQueryParams PawnObjectParams;
	PawnObjectParams.AddObjectTypesToQuery(ECC_Pawn);
	PawnObjectParams.AddObjectTypesToQuery(ECC_PhysicsBody);

	const bool bHitPawn = GetWorld()->LineTraceSingleByObjectType(
		PawnHit, TraceStart, PawnTraceEnd, PawnObjectParams, QueryParams
	);

	if (bHitPawn)
	{
		OutHit = PawnHit;
		OutBeamEnd = PawnHit.ImpactPoint;
		bOutHitPawn = true;
		return true;
	}
	else if (bHitWall)
	{
		OutHit = WallHit;
		OutBeamEnd = WallHit.ImpactPoint;
		return true;
	}

	// No hit - beam goes to max range
	OutBeamEnd = OutBeamStart + Direction * MaxBeamRange;
	return false;
}

// =============================================================================
// SpawnHarmonicBeams - create the two sweep beam Niagara components
// =============================================================================
void AShooterWeapon_Laser::SpawnHarmonicBeams()
{
	UNiagaraSystem* HarmonicFX = SecondHarmonicBeamFX ? SecondHarmonicBeamFX : LaserBeamFX;
	if (!HarmonicFX)
	{
		return;
	}

	USkeletalMeshComponent* MuzzleMesh = (PawnOwner && PawnOwner->IsPlayerControlled()) ? GetFirstPersonMesh() : GetThirdPersonMesh();
	if (!MuzzleMesh)
	{
		return;
	}

	// Spawn Beam A attached to muzzle
	ActiveHarmonicBeamA = UNiagaraFunctionLibrary::SpawnSystemAttached(
		HarmonicFX,
		MuzzleMesh,
		MuzzleSocketName,
		FVector::ZeroVector,
		FRotator::ZeroRotator,
		EAttachLocation::SnapToTarget,
		false, false, ENCPoolMethod::None
	);

	if (ActiveHarmonicBeamA)
	{
		ActiveHarmonicBeamA->AddTickPrerequisiteActor(this);
		ActiveHarmonicBeamA->SetColorParameter(FName("ColorEnergy"), SecondHarmonicColor);
		ActiveHarmonicBeamA->SetFloatParameter(FName("Scale_E"), BeamScaleE);
		ActiveHarmonicBeamA->SetVectorParameter(FName("Scale_E_Mesh"), FVector(BeamScaleEMesh));
		ActiveHarmonicBeamA->Activate();
	}

	// Spawn Beam B attached to muzzle
	ActiveHarmonicBeamB = UNiagaraFunctionLibrary::SpawnSystemAttached(
		HarmonicFX,
		MuzzleMesh,
		MuzzleSocketName,
		FVector::ZeroVector,
		FRotator::ZeroRotator,
		EAttachLocation::SnapToTarget,
		false, false, ENCPoolMethod::None
	);

	if (ActiveHarmonicBeamB)
	{
		ActiveHarmonicBeamB->AddTickPrerequisiteActor(this);
		ActiveHarmonicBeamB->SetColorParameter(FName("ColorEnergy"), SecondHarmonicColor);
		ActiveHarmonicBeamB->SetFloatParameter(FName("Scale_E"), BeamScaleE);
		ActiveHarmonicBeamB->SetVectorParameter(FName("Scale_E_Mesh"), FVector(BeamScaleEMesh));
		ActiveHarmonicBeamB->Activate();
	}
}

// =============================================================================
// DestroyHarmonicBeams - clean up sweep beam components
// =============================================================================
void AShooterWeapon_Laser::DestroyHarmonicBeams()
{
	if (ActiveHarmonicBeamA)
	{
		ActiveHarmonicBeamA->DestroyComponent();
		ActiveHarmonicBeamA = nullptr;
	}

	if (ActiveHarmonicBeamB)
	{
		ActiveHarmonicBeamB->DestroyComponent();
		ActiveHarmonicBeamB = nullptr;
	}
}

// =============================================================================
// UpdateHarmonicBeamVFX - set Niagara parameters for a single harmonic beam
// =============================================================================
void AShooterWeapon_Laser::UpdateHarmonicBeamVFX(UNiagaraComponent* Comp, const FVector& Start, const FVector& End)
{
	if (!Comp)
	{
		return;
	}

	const FVector Direction = (End - Start).GetSafeNormal();

	Comp->SetVariablePosition(FName("Beam Start"), Start);
	Comp->SetVariablePosition(FName("Beam End"), End);
	Comp->SetVectorParameter(FName("Axis"), Direction);
}
