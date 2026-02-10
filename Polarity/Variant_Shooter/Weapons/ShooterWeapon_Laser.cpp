// ShooterWeapon_Laser.cpp

#include "ShooterWeapon_Laser.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "EMFVelocityModifier.h"
#include "EMF_FieldComponent.h"
#include "../DamageTypes/DamageType_EMFWeapon.h"
#include "DrawDebugHelpers.h"

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

	UE_LOG(LogTemp, Warning, TEXT("[Laser] Fire() called. bBeamActive=%d, LaserBeamFX=%s"),
		bBeamActive, LaserBeamFX ? *LaserBeamFX->GetName() : TEXT("NULL"));

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
	const bool bHit = PerformBeamTrace(HitResult, BeamStart, BeamEnd);

	// DEBUG: Draw beam line (green = beam, blue sphere = start, red sphere = hit point)
	DrawDebugLine(GetWorld(), BeamStart, BeamEnd, FColor::Green, false, -1.0f, 0, 2.0f);
	DrawDebugSphere(GetWorld(), BeamStart, 5.0f, 6, FColor::Blue, false, -1.0f, 0, 1.0f);
	if (bHit)
	{
		DrawDebugSphere(GetWorld(), BeamEnd, 10.0f, 8, FColor::Red, false, -1.0f, 0, 2.0f);
	}

	// 3. Update beam VFX
	UpdateBeamVFX(BeamStart, BeamEnd);

	// 4. Handle hit
	if (bHit && HitResult.GetActor())
	{
		// Damage
		ApplyBeamDamage(HitResult, DeltaTime);

		// Ionization
		ApplyIonization(HitResult.GetActor(), DeltaTime);

		// Impact VFX at hit point
		UpdateImpactVFX(true, HitResult.ImpactPoint, HitResult.ImpactNormal);

		// Track current target
		CurrentHitActor = HitResult.GetActor();
	}
	else if (bHit)
	{
		// Hit a surface but no actor (wall, floor)
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
bool AShooterWeapon_Laser::PerformBeamTrace(FHitResult& OutHit, FVector& OutBeamStart, FVector& OutBeamEnd) const
{
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

	// Trace 2: Pawns (ECC_Pawn) - line trace by object type
	FHitResult PawnHit;
	FCollisionObjectQueryParams PawnObjectParams;
	PawnObjectParams.AddObjectTypesToQuery(ECC_Pawn);

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
		const bool bKilled = HitActor->IsActorBeingDestroyed();
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

	// Try UEMFVelocityModifier first (for characters - respects BaseCharge/BonusCharge system)
	if (UEMFVelocityModifier* TargetModifier = Target->FindComponentByClass<UEMFVelocityModifier>())
	{
		const float CurrentCharge = TargetModifier->GetBaseCharge();

		// Already at max positive charge - nothing to do
		if (CurrentCharge >= MaxIonizationCharge)
		{
			return;
		}

		// Add charge towards positive (ionization)
		// If negative: move towards 0, then towards positive
		// If positive: increase further
		const float NewCharge = FMath::Min(CurrentCharge + ChargeToAdd, MaxIonizationCharge);
		TargetModifier->SetBaseCharge(NewCharge);
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

	UE_LOG(LogTemp, Warning, TEXT("[Laser] ActivateBeam. LaserBeamFX=%s, LaserImpactFX=%s"),
		LaserBeamFX ? *LaserBeamFX->GetName() : TEXT("NULL"),
		LaserImpactFX ? *LaserImpactFX->GetName() : TEXT("NULL"));

	// Spawn beam Niagara component ATTACHED to weapon mesh
	// This eliminates 1-frame lag from tick ordering (component moves with weapon)
	// Spawn DEACTIVATED first, set initial params, then activate to avoid 1-frame default flash
	if (LaserBeamFX)
	{
		USkeletalMeshComponent* AttachMesh = (PawnOwner && PawnOwner->IsPlayerControlled()) ? GetFirstPersonMesh() : GetThirdPersonMesh();

		ActiveBeamComponent = UNiagaraFunctionLibrary::SpawnSystemAttached(
			LaserBeamFX,
			AttachMesh ? AttachMesh : GetRootComponent(),
			MuzzleSocketName,
			FVector::ZeroVector,
			FRotator::ZeroRotator,
			EAttachLocation::SnapToTarget,
			false,   // bAutoDestroy = false (we manage lifetime)
			false,   // bAutoActivate = false (we activate after setting params)
			ENCPoolMethod::None
		);

		// Set initial beam parameters before first render frame
		if (ActiveBeamComponent)
		{
			// Use absolute world-space transforms so beam endpoints aren't affected by
			// the weapon mesh movement (we set them explicitly each frame)
			ActiveBeamComponent->SetAbsolute(false, false, false);

			FHitResult InitHit;
			FVector InitStart, InitEnd;
			PerformBeamTrace(InitHit, InitStart, InitEnd);
			const FVector InitDir = (InitEnd - InitStart).GetSafeNormal();

			ActiveBeamComponent->SetVectorParameter(FName("Beam Start"), InitStart);
			ActiveBeamComponent->SetVectorParameter(FName("Beam End"), InitEnd);
			ActiveBeamComponent->SetVectorParameter(FName("Axis"), InitDir);
			ActiveBeamComponent->SetColorParameter(FName("ColorEnergy"), LaserColorEnergy);
			ActiveBeamComponent->SetFloatParameter(FName("Scale_E"), BeamScaleE);
			ActiveBeamComponent->SetVectorParameter(FName("Scale_E_Mesh"), FVector(BeamScaleEMesh));

			// Now activate with correct params
			ActiveBeamComponent->Activate();
		}
	}

	// Spawn impact VFX attached to weapon (persistent, starts deactivated)
	if (LaserImpactFX)
	{
		USkeletalMeshComponent* AttachMesh = (PawnOwner && PawnOwner->IsPlayerControlled()) ? GetFirstPersonMesh() : GetThirdPersonMesh();

		ActiveImpactComponent = UNiagaraFunctionLibrary::SpawnSystemAttached(
			LaserImpactFX,
			AttachMesh ? AttachMesh : GetRootComponent(),
			NAME_None,
			FVector::ZeroVector,
			FRotator::ZeroRotator,
			EAttachLocation::SnapToTarget,
			false,
			false,   // bAutoActivate = false (activated when beam hits something)
			ENCPoolMethod::None
		);

		if (ActiveImpactComponent)
		{
			// Impact needs to be positioned in world space independently
			ActiveImpactComponent->SetAbsolute(true, true, false);
		}
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
	UE_LOG(LogTemp, Warning, TEXT("[Laser] DeactivateBeam"));
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
		UE_LOG(LogTemp, Error, TEXT("[Laser] UpdateBeamVFX: ActiveBeamComponent is NULL!"));
		return;
	}

	const FVector Direction = (End - Start).GetSafeNormal();
	const float BeamLength = FVector::Dist(Start, End);

	// DEBUG: Log parameters being sent to Niagara (throttled - every 30 frames)
	static int32 DebugFrameCounter = 0;
	if (++DebugFrameCounter % 30 == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Laser] VFX Params: Start=(%.0f, %.0f, %.0f) End=(%.0f, %.0f, %.0f) Len=%.0f Axis=(%.2f, %.2f, %.2f) Scale_E=%.1f"),
			Start.X, Start.Y, Start.Z,
			End.X, End.Y, End.Z,
			BeamLength,
			Direction.X, Direction.Y, Direction.Z,
			BeamScaleE);

		UE_LOG(LogTemp, Warning, TEXT("[Laser] NiagaraComp: IsActive=%d, IsVisible=%d, IsRegistered=%d"),
			ActiveBeamComponent->IsActive(),
			ActiveBeamComponent->IsVisible(),
			ActiveBeamComponent->IsRegistered());

		// On-screen debug message
		GEngine->AddOnScreenDebugMessage(100, 0.5f, FColor::Cyan,
			FString::Printf(TEXT("LASER: Start=(%.0f,%.0f,%.0f) End=(%.0f,%.0f,%.0f) Len=%.0f"),
				Start.X, Start.Y, Start.Z,
				End.X, End.Y, End.Z,
				BeamLength));

		GEngine->AddOnScreenDebugMessage(101, 0.5f, FColor::Yellow,
			FString::Printf(TEXT("LASER Niagara: Active=%d Visible=%d Scale_E=%.1f"),
				ActiveBeamComponent->IsActive(),
				ActiveBeamComponent->IsVisible(),
				BeamScaleE));
	}

	// Core beam parameters (matching Niagara User Parameters - names with spaces as in editor)
	ActiveBeamComponent->SetVectorParameter(FName("Beam Start"), Start);
	ActiveBeamComponent->SetVectorParameter(FName("Beam End"), End);
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
