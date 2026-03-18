// KamikazeDroneNPC.cpp

#include "KamikazeDroneNPC.h"
#include "FlyingAIMovementComponent.h"
#include "FPVTiltComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/DamageEvents.h"
#include "TimerManager.h"
#include "NiagaraFunctionLibrary.h"
#include "AIController.h"
#include "GameFramework/PlayerController.h"
#include "Components/AudioComponent.h"
#include "Engine/OverlapResult.h"
#include "EMFVelocityModifier.h"
#include "EMF_FieldComponent.h"
#include "../DamageTypes/DamageType_Melee.h"
#include "../DamageTypes/DamageType_Wallslam.h"
#include "../DamageTypes/DamageType_EMFProximity.h"
#include "../DamageTypes/DamageType_KamikazeExplosion.h"
#include "../Pickups/HealthPickup.h"
#include "ShooterGameMode.h"
#include "AICombatCoordinator.h"
#include "DrawDebugHelpers.h"

// Console variable: toggle with "Kamikaze.Debug 1" in console
static TAutoConsoleVariable<int32> CVarKamikazeDebug(
	TEXT("Kamikaze.Debug"),
	0,
	TEXT("0=off, 1=log only, 2=log+visuals"),
	ECVF_Cheat);
#include "GeometryCollection/GeometryCollectionActor.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "Field/FieldSystemObjects.h"

static const TCHAR* KamikazeStateToString(EKamikazeState S)
{
	switch (S)
	{
	case EKamikazeState::Launching:    return TEXT("Launching");
	case EKamikazeState::Orbiting:     return TEXT("Orbiting");
	case EKamikazeState::Positioning:  return TEXT("Positioning");
	case EKamikazeState::Telegraphing: return TEXT("Telegraphing");
	case EKamikazeState::Attacking:    return TEXT("Attacking");
	case EKamikazeState::PostAttack:   return TEXT("PostAttack");
	case EKamikazeState::Recovery:     return TEXT("Recovery");
	case EKamikazeState::Parried:      return TEXT("Parried");
	case EKamikazeState::Dead:         return TEXT("Dead");
	default:                           return TEXT("Unknown");
	}
}

AKamikazeDroneNPC::AKamikazeDroneNPC(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Create flying movement component
	FlyingMovement = CreateDefaultSubobject<UFlyingAIMovementComponent>(TEXT("FlyingMovement"));

	// Create sphere collision
	DroneCollision = CreateDefaultSubobject<USphereComponent>(TEXT("DroneCollision"));
	DroneCollision->InitSphereRadius(CollisionRadius);
	DroneCollision->SetCollisionProfileName(FName("OverlapAllDynamic"));
	DroneCollision->SetupAttachment(RootComponent);

	// Create visual mesh
	DroneMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DroneMesh"));
	DroneMesh->SetupAttachment(DroneCollision);
	DroneMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Load placeholder sphere mesh
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere"));
	if (SphereMesh.Succeeded())
	{
		DroneMesh->SetStaticMesh(SphereMesh.Object);
		const float MeshScale = (CollisionRadius * 2.0f) / 100.0f;
		DroneMesh->SetRelativeScale3D(FVector(MeshScale));
	}

	// Create FPV tilt component
	FPVTilt = CreateDefaultSubobject<UFPVTiltComponent>(TEXT("FPVTilt"));

	// Create flight audio component
	FlightAudioComponent = CreateDefaultSubobject<UAudioComponent>(TEXT("FlightAudio"));
	FlightAudioComponent->SetupAttachment(DroneCollision);
	FlightAudioComponent->bAutoActivate = false;

	// Configure CapsuleComponent as sphere for movement collision
	GetCapsuleComponent()->SetCapsuleSize(CollisionRadius, CollisionRadius);
	GetCapsuleComponent()->SetCollisionProfileName(FName("Pawn"));

	// Hide character meshes (we use DroneMesh)
	GetMesh()->SetVisibility(false);
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Configure character movement for flying
	UCharacterMovementComponent* CMC = GetCharacterMovement();
	if (CMC)
	{
		CMC->SetMovementMode(MOVE_Flying);
		CMC->GravityScale = 0.0f;
		CMC->bOrientRotationToMovement = false;
		CMC->bUseControllerDesiredRotation = false;
		// Kamikaze drone manages its own movement via state machine (AddInputVector).
		// Spawned drones may not have an AIController assigned yet (or at all),
		// and CMC skips ALL movement when Controller==null && this==false.
		CMC->bRunPhysicsWithNoController = true;
	}

	// Drone doesn't use ragdoll
	RagdollCollisionProfile = FName("NoCollision");

	// Kamikaze is light — resists knockback less
	KnockbackDistanceMultiplier = 0.5f;

	// Low HP
	CurrentHP = 40.0f;

	// No weapon
	WeaponClass = nullptr;

	// No perception delay — telegraph serves that role
	PerceptionDelay = 0.0f;
}

// ==================== Lifecycle ====================

void AKamikazeDroneNPC::BeginPlay()
{
	Super::BeginPlay();

	// Initialize per-instance random
	InstanceRandom.Initialize(GetUniqueID());

	// NOTE: Collision sizes, profiles, and mesh scale are configured in Blueprint.
	// Do NOT override them here — BeginPlay overrides break Blueprint collision setup.

	// Disable FlyingAIMovementComponent tick — kamikaze drone manages CMC directly
	// via its own state machine. FlyingAIMovementComponent's Tick calls ApplyMovementInput()
	// which overwrites MaxFlySpeed every frame (to FlySpeed=600), fighting with our
	// Launching/Orbiting/Attacking speed values. We only need its BeginPlay (CMC init).
	if (FlyingMovement)
	{
		FlyingMovement->SetComponentTickEnabled(false);
	}

	// Initialize FPV tilt
	if (FPVTilt)
	{
		FPVTilt->Initialize(DroneMesh, AttackSpeed, GetUniqueID());
	}

	// Initialize orbit
	CurrentOrbitRadius = OrbitStartRadius;
	OrbitHeightPhaseOffset = InstanceRandom.FRandRange(0.0f, UE_TWO_PI);
	SpeedNoiseTimeOffset = InstanceRandom.FRandRange(0.0f, 100.0f);

	// Initialize orbit center to player position
	if (APawn* Player = GetPlayerPawn())
	{
		OrbitCenter = Player->GetActorLocation();
	}
	else
	{
		OrbitCenter = GetActorLocation();
	}

	// Init OrbitAngle from actual spawn position relative to orbit center
	// (each drone spawns at a different position, so angles are naturally varied in swarms)
	const FVector RelToCenter = GetActorLocation() - OrbitCenter;
	OrbitAngle = FMath::Atan2(RelToCenter.Y, RelToCenter.X);
	OrbitCumulativeAngle = 0.0f;

	CurrentState = EKamikazeState::Orbiting;
	PreviousFrameLocation = GetActorLocation();

	// Drones are NOT viscous-capturable — they are repelled by same-charge plate forces instead
	// bEnableViscousCapture stays false so plate forces are not skipped

	// Start flight sound at random position to desync multiple drones
	if (FlightAudioComponent)
	{
		if (FlightSound)
		{
			FlightAudioComponent->SetSound(FlightSound);
		}
		if (FlightAudioComponent->Sound)
		{
			const float SoundDuration = FlightAudioComponent->Sound->GetDuration();
			if (SoundDuration > 0.0f)
			{
				FlightAudioComponent->Play(FMath::FRandRange(0.0f, SoundDuration));
				FlightAudioComponent->FadeIn(0.5f, 1.0f);
			}
			else
			{
				FlightAudioComponent->FadeIn(0.5f);
			}
		}
	}
}

void AKamikazeDroneNPC::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (bIsDead)
	{
		return;
	}

	// Update flight sound pitch based on speed
	if (FlightAudioComponent && FlightAudioComponent->IsPlaying())
	{
		const float Speed = GetVelocity().Size();
		const float SpeedRange = FMath::Max(FlightPitchMaxSpeed - FlightPitchMinSpeed, 1.0f);
		const float Alpha = FMath::Clamp((Speed - FlightPitchMinSpeed) / SpeedRange, 0.0f, 1.0f);
		FlightAudioComponent->SetPitchMultiplier(FMath::Lerp(FlightPitchMin, FlightPitchMax, Alpha));
	}

	// Reset per-frame flags
	bTookDamageThisFrame = false;

	// Skip state machine while captured by EMF channeling
	if (bIsCaptured)
	{
		PreviousFrameLocation = GetActorLocation();
		return;
	}

	// State machine
	switch (CurrentState)
	{
	case EKamikazeState::Launching:
		UpdateLaunching(DeltaTime);
		break;
	case EKamikazeState::Orbiting:
		if (bIsStrafing)
		{
			UpdateStrafing(DeltaTime);
		}
		else
		{
			UpdateOrbiting(DeltaTime);
		}
		break;
	case EKamikazeState::Positioning:
		UpdatePositioning(DeltaTime);
		break;
	case EKamikazeState::Telegraphing:
		UpdateTelegraphing(DeltaTime);
		break;
	case EKamikazeState::Attacking:
		UpdateAttacking(DeltaTime);
		break;
	case EKamikazeState::PostAttack:
		UpdatePostAttack(DeltaTime);
		break;
	case EKamikazeState::Recovery:
		UpdateRecovery(DeltaTime);
		break;
	case EKamikazeState::Parried:
		UpdateParried(DeltaTime);
		break;
	default:
		break;
	}

	// --- Stuck detection: position-based (fires once per second per drone) ---
	// Uses actual position delta instead of GetVelocity() which can report stored
	// CMC velocity even when the actor hasn't moved at all.
	{
		const float PosDelta = FVector::Dist(GetActorLocation(), PreviousFrameLocation);
		// Accumulate distance over 1s window using a simple threshold check
		// If position changed less than 5cm this frame and we're in an active state, count it
		if (PosDelta < 5.0f && CurrentState != EKamikazeState::Dead && CurrentState != EKamikazeState::Telegraphing)
		{
			StuckAccumulator += DeltaTime;
			if (StuckAccumulator >= 1.0f)
			{
				StuckAccumulator = 0.0f;
				UCharacterMovementComponent* DbgCMC = GetCharacterMovement();
				// Diagnose EXACT reason CMC isn't moving
				const bool bSimPhys = DbgCMC && DbgCMC->UpdatedComponent ? DbgCMC->UpdatedComponent->IsSimulatingPhysics() : false;
				const bool bHasCtrl = GetController() != nullptr;
				const bool bRunNoCtrl = DbgCMC ? DbgCMC->bRunPhysicsWithNoController : false;
				const bool bUpdCompValid = DbgCMC && DbgCMC->UpdatedComponent != nullptr;
				const bool bIsRoot = bUpdCompValid && DbgCMC->UpdatedComponent == GetRootComponent();
				const FVector PendInput = DbgCMC ? DbgCMC->GetPendingInputVector() : FVector::ZeroVector;
				const FVector Accel = DbgCMC ? DbgCMC->GetCurrentAcceleration() : FVector::ZeroVector;
				UE_LOG(LogTemp, Error, TEXT("[KAM_STUCK %s] State=%s PosDelta=%.1f StoredVel=%.0f | CMC: Mode=%d MaxFly=%.0f Tick=%d | SimPhys=%d Ctrl=%d RunNoCtrl=%d UpdComp=%d IsRoot=%d | Input=(%.0f,%.0f,%.0f) Accel=(%.0f,%.0f,%.0f) | Pos=(%.0f,%.0f,%.0f)"),
					*GetName(), KamikazeStateToString(CurrentState), PosDelta, GetVelocity().Size(),
					DbgCMC ? (int32)DbgCMC->MovementMode.GetValue() : -1,
					DbgCMC ? DbgCMC->MaxFlySpeed : -1.f,
					DbgCMC ? (int32)DbgCMC->IsComponentTickEnabled() : -1,
					(int32)bSimPhys, (int32)bHasCtrl, (int32)bRunNoCtrl,
					(int32)bUpdCompValid, (int32)bIsRoot,
					PendInput.X, PendInput.Y, PendInput.Z,
					Accel.X, Accel.Y, Accel.Z,
					GetActorLocation().X, GetActorLocation().Y, GetActorLocation().Z);
			}
		}
		else
		{
			StuckAccumulator = 0.0f;
		}
	}

	// Update FPV tilt every frame (skip when parried — mesh spins freely)
	if (FPVTilt && !bIsDead && CurrentState != EKamikazeState::Parried)
	{
		const FVector Vel = GetVelocity();
		const float Speed = Vel.Size();
		// Approximate acceleration from velocity change (CMC doesn't expose it directly)
		const FVector Accel = GetCharacterMovement() ? GetCharacterMovement()->GetCurrentAcceleration() : FVector::ZeroVector;
		FPVTilt->SetMovementState(Speed, Vel, Accel);
	}

	// Orient actor to face velocity direction (yaw only) — skip when parried (mesh spins freely)
	const FVector Vel = GetVelocity();
	if (!Vel.IsNearlyZero(10.0f) && CurrentState != EKamikazeState::Parried)
	{
		const FRotator CurrentRot = GetActorRotation();
		const FRotator TargetRot = Vel.Rotation();
		// Use yaw interp speed appropriate to current state
		const float YawSpeed = (CurrentState == EKamikazeState::Attacking || CurrentState == EKamikazeState::PostAttack)
			? AttackTurnRate * AttackTurnRateMultiplier
			: OrbitMaxTurnRate;
		const FRotator NewRot = FMath::RInterpTo(CurrentRot, FRotator(0.0f, TargetRot.Yaw, 0.0f), DeltaTime, YawSpeed / 45.0f);
		SetActorRotation(NewRot);
	}

	// ==================== DEBUG VISUALIZATION ====================
#if ENABLE_DRAW_DEBUG
	const int32 DebugLevel = CVarKamikazeDebug.GetValueOnGameThread();
	if (DebugLevel >= 2)
	{
		const FVector MyLoc = GetActorLocation();
		const float MySpeed = GetVelocity().Size();
		const FVector MyVel = GetVelocity();

		// --- State + speed text above drone ---
		const FString StateStr = FString::Printf(TEXT("%s | Spd:%.0f | Rad:%.0f | H:%.0f | OrbitT:%.1f"),
			KamikazeStateToString(CurrentState), MySpeed, CurrentOrbitRadius, MyLoc.Z - OrbitCenter.Z, OrbitElapsedTime);

		DrawDebugString(GetWorld(), MyLoc + FVector(0, 0, 80), StateStr, nullptr, FColor::White, 0.0f, true, 1.0f);

		// --- Velocity arrow (cyan) ---
		if (!MyVel.IsNearlyZero())
		{
			DrawDebugDirectionalArrow(GetWorld(), MyLoc, MyLoc + MyVel.GetSafeNormal() * 150.0f,
				15.0f, FColor::Cyan, false, 0.0f, 0, 2.0f);
		}

		// --- Orbit center (yellow sphere) ---
		DrawDebugSphere(GetWorld(), OrbitCenter, 30.0f, 8, FColor::Yellow, false, 0.0f, 0, 1.5f);

		// --- Orbit path (yellow circle at orbit height) ---
		const FVector CircleCenter(OrbitCenter.X, OrbitCenter.Y, OrbitCenter.Z + OrbitBaseHeight);
		DrawDebugCircle(GetWorld(), CircleCenter, CurrentOrbitRadius, 32,
			FColor::Yellow, false, 0.0f, 0, 1.0f, FVector::YAxisVector, FVector::XAxisVector, false);

		// --- Line from drone to orbit center (yellow dotted) ---
		DrawDebugLine(GetWorld(), MyLoc, OrbitCenter, FColor::Yellow, false, 0.0f, 0, 0.5f);

		if (CurrentState == EKamikazeState::Orbiting)
		{
			// --- Orbit target position (green sphere) ---
			const float SM = CurrentOrbitRadius;
			const float Sm = CurrentOrbitRadius * (1.0f - OrbitEccentricity);
			const float OTx = OrbitCenter.X + SM * FMath::Cos(OrbitAngle);
			const float OTy = OrbitCenter.Y + Sm * FMath::Sin(OrbitAngle);
			const float OTz = OrbitCenter.Z + OrbitBaseHeight + OrbitHeightAmplitude * FMath::Sin(OrbitAngle + OrbitHeightPhaseOffset);
			const FVector OrbitTarget(OTx, OTy, OTz);
			DrawDebugSphere(GetWorld(), OrbitTarget, 20.0f, 6, FColor::Green, false, 0.0f, 0, 2.0f);

			// --- Line from drone to orbit target (green) ---
			DrawDebugLine(GetWorld(), MyLoc, OrbitTarget, FColor::Green, false, 0.0f, 0, 1.5f);

			// --- Distance to orbit target ---
			const float DistToTarget = FVector::Dist(MyLoc, OrbitTarget);
			const FString OrbitStr = FString::Printf(TEXT("DistToTarget: %.0f"), DistToTarget);
			DrawDebugString(GetWorld(), MyLoc + FVector(0, 0, 60), OrbitStr, nullptr, FColor::Green, 0.0f, true, 0.8f);
		}

		if (CurrentState == EKamikazeState::Telegraphing)
		{
			// --- Telegraph: draw Bezier curve and control points ---
			const float DbgAlpha = FMath::Clamp(StateTimer / FMath::Max(TelegraphDuration, 0.01f), 0.0f, 1.0f);

			const FVector P0 = TelegraphStartPos;
			const FVector P1 = TelegraphPhantomOrbitCenter;
			const FVector P3 = TelegraphStartPos + TelegraphAttackDir * TelegraphPhantomOrbitAngle;
			const FVector P2 = P3 - TelegraphAttackDir * TelegraphPhantomOrbitSpeed;

			// Draw Bezier curve as segments (orange = traveled, yellow = remaining)
			constexpr int32 NumSegments = 20;
			FVector PrevPt = P0;
			for (int32 i = 1; i <= NumSegments; ++i)
			{
				const float st = (float)i / (float)NumSegments;
				const float su = 1.0f - st;
				const FVector Pt = su*su*su * P0 + 3.0f*su*su*st * P1 + 3.0f*su*st*st * P2 + st*st*st * P3;
				DrawDebugLine(GetWorld(), PrevPt, Pt, (st <= DbgAlpha) ? FColor::Orange : FColor::Yellow, false, 0.0f, 0, 2.0f);
				PrevPt = Pt;
			}

			// Control points: P0 green, P1 blue (orbit tangent), P2 red (attack approach), P3 magenta (end)
			DrawDebugSphere(GetWorld(), P0, 15.0f, 4, FColor::Green, false, 0.0f, 0, 1.5f);
			DrawDebugSphere(GetWorld(), P1, 15.0f, 4, FColor::Blue, false, 0.0f, 0, 1.5f);
			DrawDebugSphere(GetWorld(), P2, 15.0f, 4, FColor::Red, false, 0.0f, 0, 1.5f);
			DrawDebugSphere(GetWorld(), P3, 15.0f, 4, FColor::Magenta, false, 0.0f, 0, 1.5f);

			// Control polygon (tangent handles)
			DrawDebugLine(GetWorld(), P0, P1, FColor::Blue, false, 0.0f, 0, 0.5f);
			DrawDebugLine(GetWorld(), P2, P3, FColor::Red, false, 0.0f, 0, 0.5f);

			// Attack target
			DrawDebugSphere(GetWorld(), AttackTargetPosition, 25.0f, 8, FColor::Magenta, false, 0.0f, 0, 2.0f);

			// Progress text
			const FString BezierStr = FString::Printf(TEXT("Bezier t: %.2f | Speed: %.0f"), DbgAlpha, MySpeed);
			DrawDebugString(GetWorld(), MyLoc + FVector(0, 0, 60), BezierStr, nullptr, FColor::Orange, 0.0f, true, 1.0f);
		}

		if (CurrentState == EKamikazeState::Attacking)
		{
			// --- Attack target (red sphere) ---
			DrawDebugSphere(GetWorld(), AttackTargetPosition, 25.0f, 8, FColor::Red, false, 0.0f, 0, 3.0f);

			// --- Line from drone to attack target (red) ---
			DrawDebugLine(GetWorld(), MyLoc, AttackTargetPosition, FColor::Red, false, 0.0f, 0, 2.0f);
		}

		if (CurrentState == EKamikazeState::PostAttack)
		{
			// --- Direction arrow (orange) ---
			DrawDebugDirectionalArrow(GetWorld(), MyLoc, MyLoc + AttackDirection * 300.0f,
				20.0f, FColor::Orange, false, 0.0f, 0, 3.0f);
		}

		// --- Ground check: raycast down to show height above floor ---
		FHitResult GroundHit;
		if (GetWorld()->LineTraceSingleByChannel(GroundHit, MyLoc, MyLoc - FVector(0, 0, 2000), ECC_WorldStatic))
		{
			const float HeightAboveGround = MyLoc.Z - GroundHit.Location.Z;
			DrawDebugLine(GetWorld(), MyLoc, GroundHit.Location, FColor::Magenta, false, 0.0f, 0, 0.5f);
			DrawDebugString(GetWorld(), MyLoc + FVector(0, 0, 40), FString::Printf(TEXT("Floor:%.0f cm"), HeightAboveGround),
				nullptr, FColor::Magenta, 0.0f, true, 0.8f);
		}

		// --- CMC movement mode ---
		if (UCharacterMovementComponent* CMC = GetCharacterMovement())
		{
			const FString MoveStr = FString::Printf(TEXT("CMC: Mode=%d MaxFly=%.0f Accel=%.0f Grav=%.1f"),
				(int32)CMC->MovementMode.GetValue(), CMC->MaxFlySpeed, CMC->MaxAcceleration, CMC->GravityScale);
			DrawDebugString(GetWorld(), MyLoc + FVector(0, 0, 100), MoveStr, nullptr, FColor::Silver, 0.0f, true, 0.7f);
		}
	}
#endif

	// Update previous location for sweep collision detection (must be last in Tick)
	PreviousFrameLocation = GetActorLocation();
}

// ==================== State Machine ====================

void AKamikazeDroneNPC::SetState(EKamikazeState NewState)
{
	const EKamikazeState OldState = CurrentState;
	CurrentState = NewState;
	StateTimer = 0.0f;

	// Reset orbit timer when entering orbit
	if (NewState == EKamikazeState::Orbiting)
	{
		OrbitElapsedTime = 0.0f;
	}

	if (CVarKamikazeDebug.GetValueOnGameThread() >= 1)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Kamikaze %s] State: %s -> %s | Pos=(%.0f,%.0f,%.0f) Vel=%.0f"),
			*GetName(), KamikazeStateToString(OldState), KamikazeStateToString(NewState),
			GetActorLocation().X, GetActorLocation().Y, GetActorLocation().Z,
			GetVelocity().Size());
	}
}

void AKamikazeDroneNPC::UpdateLaunching(float DeltaTime)
{
	StateTimer += DeltaTime;

	UCharacterMovementComponent* CMC = GetCharacterMovement();
	if (!CMC)
	{
		SetState(EKamikazeState::Orbiting);
		return;
	}

	// --- Direct movement: CMC tick is disabled, we drive position ourselves ---
	// CMC->Velocity is used purely as velocity storage.
	// This bypasses the tick-ordering bug where CMC::Tick runs before Actor::Tick,
	// braking the velocity to zero before AddInputVector can provide input.

	FVector CurrentVel = CMC->Velocity;
	float CurrentSpeed = CurrentVel.Size();
	FVector CurrentDir = (CurrentSpeed > 1.f) ? CurrentVel / CurrentSpeed : GetActorForwardVector();

	// --- FPV PID stabilization: exponential speed decay toward CruiseSpeed ---
	const float NewSpeed = FMath::FInterpTo(CurrentSpeed, CruiseSpeed, DeltaTime, LaunchDecayRate);

	// --- Gentle FPV arc toward orbit area ---
	FVector DesiredDir = CurrentDir;

	APawn* Player = GetPlayerPawn();
	if (Player)
	{
		// Smoothly track player position for orbit center
		OrbitCenter = FMath::VInterpTo(OrbitCenter, Player->GetActorLocation(), DeltaTime, 2.0f);

		// Target: orbit-radius distance from player, at orbit altitude
		const FVector ToPlayer = Player->GetActorLocation() - GetActorLocation();
		const FVector ToPlayerFlat = FVector(ToPlayer.X, ToPlayer.Y, 0.0f);
		const float DistToPlayer = ToPlayerFlat.Size();

		FVector DesiredPos;
		if (DistToPlayer > OrbitStartRadius * 0.5f)
		{
			// Far: arc toward player vicinity at orbit radius
			DesiredPos = Player->GetActorLocation()
				+ (-ToPlayerFlat.GetSafeNormal() * OrbitStartRadius)
				+ FVector(0.0f, 0.0f, OrbitBaseHeight);
		}
		else
		{
			// Near: just aim for orbit altitude
			DesiredPos = GetActorLocation();
			DesiredPos.Z = Player->GetActorLocation().Z + OrbitBaseHeight;
		}

		// Rate-limited turn (LaunchSteerRate deg/s) — mimics PID yaw/pitch correction
		const FVector ToDesired = (DesiredPos - GetActorLocation()).GetSafeNormal();
		DesiredDir = FMath::VInterpNormalRotationTo(CurrentDir, ToDesired, DeltaTime, LaunchSteerRate);
	}

	// --- Update velocity and move actor directly ---
	const FVector NewVel = DesiredDir * NewSpeed;
	CMC->Velocity = NewVel;

	const FVector MoveDelta = NewVel * DeltaTime;
	FHitResult Hit;
	CMC->MoveUpdatedComponent(MoveDelta, GetActorRotation(), true, &Hit);
	if (Hit.bBlockingHit)
	{
		// Compute slide direction: remove the component that goes into the surface
		const FVector SlideDir = FVector::VectorPlaneProject(MoveDelta, Hit.Normal).GetSafeNormal();
		const float RemainingTime = 1.f - Hit.Time;
		if (RemainingTime > SMALL_NUMBER && !SlideDir.IsNearlyZero())
		{
			const FVector SlideDelta = SlideDir * (NewSpeed * RemainingTime * DeltaTime);
			CMC->MoveUpdatedComponent(SlideDelta, GetActorRotation(), true, &Hit);
		}
	}

	// --- Periodic diagnostics (every 0.25s, per-instance timer) ---
	{
		const float LogInterval = 0.25f;
		// Use StateTimer modulo for per-instance timing (no static!)
		const float PrevT = StateTimer - DeltaTime;
		if (FMath::FloorToInt(StateTimer / LogInterval) != FMath::FloorToInt(PrevT / LogInterval))
		{
			UE_LOG(LogTemp, Warning, TEXT("[KAM_LAUNCH %s] t=%.2f | Speed=%.0f→%.0f (target=%.0f) | Dir=%s | Pos=(%.0f,%.0f,%.0f)"),
				*GetName(), StateTimer,
				CurrentSpeed, NewSpeed, CruiseSpeed,
				*DesiredDir.ToCompactString(),
				GetActorLocation().X, GetActorLocation().Y, GetActorLocation().Z);
		}
	}

	// --- Transition to orbit when speed settles near CruiseSpeed ---
	const bool bSpeedStabilized = (NewSpeed <= CruiseSpeed * 1.15f) && (NewSpeed > CruiseSpeed * 0.5f);
	const bool bMinTimeElapsed = StateTimer >= 0.3f;

	if ((bSpeedStabilized && bMinTimeElapsed) || StateTimer >= MaxLaunchStabilizationTime)
	{
		// Snap orbit parameters to current spatial relationship
		if (Player)
		{
			OrbitCenter = Player->GetActorLocation();
		}
		const FVector RelToCenter = GetActorLocation() - OrbitCenter;
		OrbitAngle = FMath::Atan2(RelToCenter.Y, RelToCenter.X);
		OrbitCumulativeAngle = 0.0f;

		// Set orbit radius from actual distance (clamped to valid range)
		CurrentOrbitRadius = FVector::Dist2D(GetActorLocation(), OrbitCenter);
		CurrentOrbitRadius = FMath::Clamp(CurrentOrbitRadius, OrbitMinRadius, OrbitStartRadius);

		// Re-enable CMC for Orbiting state — smooth handoff with current velocity
		CMC->SetMovementMode(MOVE_Flying);
		CMC->MaxFlySpeed = CruiseSpeed;
		CMC->SetComponentTickEnabled(true);

		const float EntryHeightAbovePlayer = GetActorLocation().Z - OrbitCenter.Z;
		const float ExpectedHeight = OrbitBaseHeight;
		UE_LOG(LogTemp, Warning, TEXT("[Kamikaze %s] Launch->Orbit | Time=%.2f Speed=%.0f->%.0f Radius=%.0f CruiseSpeed=%.0f | HeightAbovePlayer=%.0f (expected=%.0f, err=%.0f)"),
			*GetName(), StateTimer, LaunchInitialSpeed, NewSpeed, CurrentOrbitRadius, CruiseSpeed,
			EntryHeightAbovePlayer, ExpectedHeight, ExpectedHeight - EntryHeightAbovePlayer);

		SetState(EKamikazeState::Orbiting);
	}
}

void AKamikazeDroneNPC::InitiateLaunch(const FVector& LaunchVelocity)
{
	UCharacterMovementComponent* CMC = GetCharacterMovement();
	if (!CMC) return;

	LaunchInitialSpeed = LaunchVelocity.Size();

	// Store velocity in CMC->Velocity (used as storage during Launching).
	// CMC tick is DISABLED during launch — we move the actor directly via
	// MoveUpdatedComponent to bypass tick-ordering issues where CMC brakes
	// velocity to zero before Actor::Tick can call AddInputVector.
	CMC->Velocity = LaunchVelocity;
	CMC->SetComponentTickEnabled(false);

	// Orient drone to face launch direction
	if (!LaunchVelocity.IsNearlyZero())
	{
		SetActorRotation(LaunchVelocity.Rotation());
	}

	// Initialize orbit center from current position — will track toward player during launch
	OrbitCenter = GetActorLocation();

	SetState(EKamikazeState::Launching);
	StateTimer = 0.0f;

	UE_LOG(LogTemp, Warning, TEXT("[Kamikaze %s] InitiateLaunch | Speed=%.0f Dir=%s CruiseSpeed=%.0f | Pos=(%.0f,%.0f,%.0f)"),
		*GetName(), LaunchInitialSpeed, *LaunchVelocity.GetSafeNormal().ToCompactString(), CruiseSpeed,
		GetActorLocation().X, GetActorLocation().Y, GetActorLocation().Z);
}

void AKamikazeDroneNPC::UpdateOrbiting(float DeltaTime)
{
	APawn* Player = GetPlayerPawn();
	if (!Player)
	{
		return;
	}

	// --- Evaluate orbit quality (once per second) ---
	OrbitEvaluationTimer += DeltaTime;
	if (OrbitEvaluationTimer >= 1.0f)
	{
		OrbitEvaluationTimer = 0.0f;
		EvaluateOrbitQuality();
		if (bIsStrafing) return; // switched to strafe
	}

	// --- Update orbit center (smoothed tracking of player) ---
	TimeSinceOrbitCenterUpdate += DeltaTime;
	if (TimeSinceOrbitCenterUpdate >= OrbitCenterUpdateInterval)
	{
		TimeSinceOrbitCenterUpdate = 0.0f;
		const FVector PlayerPos = Player->GetActorLocation();
		OrbitCenter = FMath::VInterpTo(OrbitCenter, PlayerPos, OrbitCenterUpdateInterval, 500.0f / OrbitCenterUpdateInterval);
	}

	// --- Compute effective speed with noise ---
	const float SpeedNoise = SpeedNoiseAmplitude * FMath::Sin((GetWorld()->GetTimeSeconds() + SpeedNoiseTimeOffset) * 2.7f);
	const float EffectiveSpeed = CruiseSpeed * (1.0f + SpeedNoise);
	const float AngularSpeed = EffectiveSpeed / FMath::Max(CurrentOrbitRadius, 100.0f);

	// --- Sync OrbitAngle to drone's actual angular position ---
	// This prevents the orbit target from racing ahead when the drone can't keep up
	const FVector RelativePos = GetActorLocation() - OrbitCenter;
	const float ActualAngle = FMath::Atan2(RelativePos.Y, RelativePos.X);

	// Compute how much the drone actually advanced (signed, handles wrap-around)
	float AngleDelta = ActualAngle - OrbitAngle;
	if (AngleDelta > UE_PI) AngleDelta -= UE_TWO_PI;
	if (AngleDelta < -UE_PI) AngleDelta += UE_TWO_PI;

	// Accumulate positive (forward) movement for lap counting
	if (AngleDelta > 0.0f)
	{
		OrbitCumulativeAngle += AngleDelta;
	}

	// Snap OrbitAngle to actual drone angle
	OrbitAngle = ActualAngle;

	// --- Lap completion: shrink radius ---
	if (OrbitCumulativeAngle >= UE_TWO_PI)
	{
		OrbitCumulativeAngle -= UE_TWO_PI;
		CurrentOrbitRadius = FMath::Max(CurrentOrbitRadius - OrbitShrinkPerLap, OrbitMinRadius);
	}

	// --- Lead target: point slightly ahead on the orbit ---
	// 0.5s of angular lead — drone chases a point ahead on the curve, not behind it
	const float LeadAngle = AngularSpeed * 0.5f;
	const float TargetAngle = OrbitAngle + LeadAngle;

	const float SemiMajor = CurrentOrbitRadius;
	const float SemiMinor = CurrentOrbitRadius * (1.0f - OrbitEccentricity);
	const float TargetX = OrbitCenter.X + SemiMajor * FMath::Cos(TargetAngle);
	const float TargetY = OrbitCenter.Y + SemiMinor * FMath::Sin(TargetAngle);

	// Vertical sinusoid (use TargetAngle for consistent height at lead point)
	const float HeightOscillation = OrbitHeightAmplitude * FMath::Sin(TargetAngle + OrbitHeightPhaseOffset);
	const float TargetZ = OrbitCenter.Z + OrbitBaseHeight + HeightOscillation;

	const FVector TargetOrbitPos(TargetX, TargetY, TargetZ);

	// --- Geometry check ---
	TimeSinceGeometryCheck += DeltaTime;
	if (TimeSinceGeometryCheck >= GeometryCheckInterval)
	{
		TimeSinceGeometryCheck = 0.0f;

		const FVector ForwardDir = (TargetOrbitPos - GetActorLocation()).GetSafeNormal();
		FHitResult Hit;
		FCollisionQueryParams QueryParams;
		QueryParams.AddIgnoredActor(this);

		const FVector GeoRayEnd = GetActorLocation() + ForwardDir * 200.0f;
		if (GetWorld()->LineTraceSingleByChannel(Hit, GetActorLocation(), GeoRayEnd, ECC_WorldStatic, QueryParams))
		{
			// Obstacle ahead — track time unable to orbit
			OrbitForcedTimer += GeometryCheckInterval;

			if (CVarKamikazeDebug.GetValueOnGameThread() >= 1)
			{
				UE_LOG(LogTemp, Log, TEXT("[Kamikaze %s] Orbit geometry HIT: %s (%.0fcm ahead) ForcedTimer=%.1f"),
					*GetName(),
					Hit.GetActor() ? *Hit.GetActor()->GetName() : TEXT("null"),
					Hit.Distance, OrbitForcedTimer);
			}
		}
		else
		{
			OrbitForcedTimer = FMath::Max(OrbitForcedTimer - GeometryCheckInterval, 0.0f);
		}

		// Forced attack if can't maintain orbit for 1.5s
		if (OrbitForcedTimer >= 1.5f && CurrentOrbitRadius <= MinOrbitSpaceThreshold)
		{
			if (CVarKamikazeDebug.GetValueOnGameThread() >= 1)
			{
				UE_LOG(LogTemp, Warning, TEXT("[Kamikaze %s] FORCED ATTACK (orbit blocked %.1fs, radius=%.0f < threshold=%.0f)"),
					*GetName(), OrbitForcedTimer, CurrentOrbitRadius, MinOrbitSpaceThreshold);
			}
			bOrbitForced = true;
			BeginTelegraph(false);
			return;
		}
	}

	// --- Move drone toward orbit position (with FPV jitter) ---
	// Separate horizontal (orbit path) and vertical (altitude) control so that
	// limited MaxFlySpeed doesn't starve height correction when the drone is far
	// below the target altitude.
	if (UCharacterMovementComponent* CMC = GetCharacterMovement())
	{
		const FVector MyLoc = GetActorLocation();
		const float HeightError = TargetZ - MyLoc.Z;

		// Horizontal direction: XY only (orbit path)
		const FVector HorizTarget(TargetOrbitPos.X, TargetOrbitPos.Y, MyLoc.Z);
		const FVector HorizDir = (HorizTarget - MyLoc).GetSafeNormal();

		// Blend vertical correction proportionally to height error.
		// At 0 error -> pure horizontal; at >=200cm error -> strong vertical pull.
		const float VerticalUrgency = FMath::Clamp(FMath::Abs(HeightError) / 200.0f, 0.0f, 1.0f);
		const float VerticalComponent = FMath::Sign(HeightError) * VerticalUrgency * 0.6f;

		FVector MoveDir = (HorizDir + FVector(0.0f, 0.0f, VerticalComponent)).GetSafeNormal();

		const float T = GetWorld()->GetTimeSeconds() + SpeedNoiseTimeOffset;
		const FVector Right = FVector::CrossProduct(MoveDir, FVector::UpVector).GetSafeNormal();
		const FVector Up = FVector::CrossProduct(Right, MoveDir);
		const float JitterRight = FMath::Sin(T * 11.3f) * 0.6f + FMath::Sin(T * 7.1f) * 0.4f;
		const float JitterUp    = FMath::Sin(T * 9.7f) * 0.6f + FMath::Sin(T * 5.3f) * 0.4f;
		const FVector NoisyDir = (MoveDir + (Right * JitterRight + Up * JitterUp) * AttackJitterAmplitude).GetSafeNormal();

		const float OrbitJitterSpeed = AttackSpeedJitter * FMath::Sin(T * 13.1f + 2.0f);
		CMC->AddInputVector(NoisyDir);
		CMC->MaxFlySpeed = EffectiveSpeed * (1.0f + OrbitJitterSpeed);
	}

	// --- Height diagnostics (every 0.5s) ---
	if (CVarKamikazeDebug.GetValueOnGameThread() >= 1)
	{
		// Per-instance timer using OrbitElapsedTime (already accumulates)
		const float LogInterval = 0.5f;
		const float PrevElapsed = OrbitElapsedTime; // will be incremented below
		const float CurElapsed = PrevElapsed + DeltaTime;
		if (FMath::FloorToInt(CurElapsed / LogInterval) != FMath::FloorToInt(PrevElapsed / LogInterval))
		{
			const FVector MyLoc = GetActorLocation();
			const FVector PlayerPos = Player->GetActorLocation();
			const float DroneZ = MyLoc.Z;
			const float PlayerZ = PlayerPos.Z;
			const float OrbitCenterZ = OrbitCenter.Z;
			const float HeightOsc = OrbitHeightAmplitude * FMath::Sin((OrbitAngle + (AngularSpeed * 0.5f)) + OrbitHeightPhaseOffset);
			const float ComputedTargetZ = OrbitCenterZ + OrbitBaseHeight + HeightOsc;
			const float HeightErr = ComputedTargetZ - DroneZ;
			const float ActualVelZ = GetVelocity().Z;
			UCharacterMovementComponent* DbgCMC = GetCharacterMovement();
			const float CMCMaxFly = DbgCMC ? DbgCMC->MaxFlySpeed : -1.f;
			const float CMCMaxAccel = DbgCMC ? DbgCMC->MaxAcceleration : -1.f;
			const FVector CMCVel = DbgCMC ? DbgCMC->Velocity : FVector::ZeroVector;
			const FVector CMCAccel = DbgCMC ? DbgCMC->GetCurrentAcceleration() : FVector::ZeroVector;

			UE_LOG(LogTemp, Warning,
				TEXT("[KAM_HEIGHT %s] DroneZ=%.0f PlayerZ=%.0f OrbCenterZ=%.0f | TargetZ=%.0f (base=%.0f + osc=%.0f) | Err=%.0f | VelZ=%.0f CMCVelZ=%.0f | AccelZ=%.0f | MaxFly=%.0f MaxAccel=%.0f | Speed=%.0f"),
				*GetName(), DroneZ, PlayerZ, OrbitCenterZ,
				ComputedTargetZ, OrbitBaseHeight, HeightOsc, HeightErr,
				ActualVelZ, CMCVel.Z, CMCAccel.Z,
				CMCMaxFly, CMCMaxAccel, GetVelocity().Size());
		}
	}

	// --- Token-based attack check ---
	OrbitElapsedTime += DeltaTime;
	if (OrbitElapsedTime >= MinOrbitTimeBeforeAttack)
	{
		AAICombatCoordinator* Coordinator = AAICombatCoordinator::GetCoordinator(this);
		if (Coordinator && Coordinator->RequestAttackToken(this, EAttackTokenType::Kamikaze))
		{
			bIsRetaliating = false;
			SetState(EKamikazeState::Positioning);
			return;
		}
	}

	// --- Proximity attack check ---
	const float DistToPlayer = FVector::Dist(GetActorLocation(), Player->GetActorLocation());
	if (CurrentOrbitRadius <= ProximityAttackRadius)
	{
		ProximityTimer += DeltaTime;
		if (ProximityTimer >= ProximityAttackDelay)
		{
			// Emergency attack — skip positioning, go straight to telegraph
			BeginTelegraph(false);
			return;
		}
	}
	else
	{
		ProximityTimer = 0.0f;
	}
}

void AKamikazeDroneNPC::UpdatePositioning(float DeltaTime)
{
	StateTimer += DeltaTime;

	APawn* Player = GetPlayerPawn();
	if (!Player)
	{
		BeginTelegraph(bIsRetaliating);
		return;
	}

	// Compute frontal attack point: player position + camera forward (XY) * current orbit radius, at orbit height
	FVector FrontalTarget = Player->GetActorLocation();
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
	{
		const FVector CamFwd = PC->GetControlRotation().Vector();
		const FVector CamFwdXY = FVector(CamFwd.X, CamFwd.Y, 0.0f).GetSafeNormal();
		if (!CamFwdXY.IsNearlyZero())
		{
			FrontalTarget += CamFwdXY * CurrentOrbitRadius;
		}
	}
	FrontalTarget.Z = Player->GetActorLocation().Z + OrbitBaseHeight;

	const FVector MyLoc = GetActorLocation();
	const float DistToTarget = FVector::Dist(MyLoc, FrontalTarget);

#if ENABLE_DRAW_DEBUG
	// Frontal target point (magenta sphere) + line from drone to it
	DrawDebugSphere(GetWorld(), FrontalTarget, 40.0f, 8, FColor::Magenta, false, 0.0f, 0, 2.0f);
	DrawDebugLine(GetWorld(), MyLoc, FrontalTarget, FColor::Magenta, false, 0.0f, 0, 1.5f);
	// Arrival threshold sphere (yellow wireframe)
	DrawDebugSphere(GetWorld(), FrontalTarget, PositioningArrivalThreshold, 12, FColor::Yellow, false, 0.0f, 0, 1.0f);
#endif

	// Arrived or timed out — begin telegraph from here
	if (DistToTarget <= PositioningArrivalThreshold || StateTimer >= PositioningMaxTime)
	{
		BeginTelegraph(bIsRetaliating);
		return;
	}

	// Fly toward frontal point with limited turn rate (same pattern as orbit movement)
	if (UCharacterMovementComponent* CMC = GetCharacterMovement())
	{
		CMC->MaxFlySpeed = PositioningSpeed;

		const FVector DesiredDir = (FrontalTarget - MyLoc).GetSafeNormal();
		const FVector CurrentDir = GetVelocity().IsNearlyZero(10.0f) ? GetActorForwardVector() : GetVelocity().GetSafeNormal();
		const FVector SteeringDir = FMath::VInterpNormalRotationTo(CurrentDir, DesiredDir, DeltaTime, PositioningTurnRate);

		// FPV jitter for visual consistency
		const float T = GetWorld()->GetTimeSeconds() + SpeedNoiseTimeOffset;
		const FVector Right = FVector::CrossProduct(SteeringDir, FVector::UpVector).GetSafeNormal();
		const FVector Up = FVector::CrossProduct(Right, SteeringDir);
		const float JitterRight = FMath::Sin(T * 11.3f) * 0.6f + FMath::Sin(T * 7.1f) * 0.4f;
		const float JitterUp    = FMath::Sin(T * 9.7f) * 0.6f + FMath::Sin(T * 5.3f) * 0.4f;
		const FVector NoisyDir = (SteeringDir + (Right * JitterRight + Up * JitterUp) * AttackJitterAmplitude * 0.5f).GetSafeNormal();

		CMC->AddInputVector(NoisyDir);
	}
}

void AKamikazeDroneNPC::UpdateTelegraphing(float DeltaTime)
{
	StateTimer += DeltaTime;
	const float RawAlpha = FMath::Clamp(StateTimer / FMath::Max(TelegraphDuration, 0.01f), 0.0f, 1.0f);

	// --- Cubic Bezier: smooth curve from orbit tangent to attack direction ---
	// Control points stored in BeginTelegraph (repurposed phantom variables):
	//   P0 = TelegraphStartPos
	//   P1 = TelegraphPhantomOrbitCenter  (orbit tangent pull)
	//   P3 = TelegraphStartPos + TelegraphAttackDir * TelegraphPhantomOrbitAngle
	//   P2 = P3 - TelegraphAttackDir * TelegraphPhantomOrbitSpeed
	const FVector P0 = TelegraphStartPos;
	const FVector P1 = TelegraphPhantomOrbitCenter;
	const FVector P3 = TelegraphStartPos + TelegraphAttackDir * TelegraphPhantomOrbitAngle;
	const FVector P2 = P3 - TelegraphAttackDir * TelegraphPhantomOrbitSpeed;

	const float t = RawAlpha;
	const float u = 1.0f - t;
	FVector BezierPos = u*u*u * P0 + 3.0f*u*u*t * P1 + 3.0f*u*t*t * P2 + t*t*t * P3;

	// --- FPV jitter during telegraph, ramping up with t² (quiet start, noisy end) ---
	{
		const float T = GetWorld()->GetTimeSeconds() + SpeedNoiseTimeOffset;
		const FVector BezierTangent = (3.0f*u*u*(P1-P0) + 6.0f*u*t*(P2-P1) + 3.0f*t*t*(P3-P2)).GetSafeNormal();
		const FVector Right = FVector::CrossProduct(BezierTangent, FVector::UpVector).GetSafeNormal();
		const FVector Up = FVector::CrossProduct(Right, BezierTangent);
		const float JitterRight = FMath::Sin(T * 11.3f) * 0.6f + FMath::Sin(T * 7.1f) * 0.4f;
		const float JitterUp    = FMath::Sin(T * 9.7f) * 0.6f + FMath::Sin(T * 5.3f) * 0.4f;
		// t*t ramp: silent at start, full jitter near end; scale by CruiseSpeed for world-space amplitude
		const float JitterScale = t * t * AttackJitterAmplitude * CruiseSpeed;
		BezierPos += (Right * JitterRight + Up * JitterUp) * JitterScale;
	}

	SetActorLocation(BezierPos, false, nullptr, ETeleportType::TeleportPhysics);

	// --- Compute virtual velocity for FPVTilt and actor rotation ---
	const FVector VirtualVelocity = (DeltaTime > SMALL_NUMBER)
		? (BezierPos - TelegraphPrevPos) / DeltaTime
		: TelegraphAttackDir * CruiseSpeed;
	TelegraphPrevPos = BezierPos;

	if (UCharacterMovementComponent* CMC = GetCharacterMovement())
	{
		CMC->Velocity = VirtualVelocity;
	}

	// --- Per-frame position log ---
	if (CVarKamikazeDebug.GetValueOnGameThread() >= 1)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Kamikaze %s] Telegraph Bezier | t=%.2f Pos=(%.0f,%.0f,%.0f) Spd=%.0f"),
			*GetName(), t,
			BezierPos.X, BezierPos.Y, BezierPos.Z,
			VirtualVelocity.Size());
	}

	// --- Commit when complete ---
	if (RawAlpha >= 1.0f)
	{
		if (CVarKamikazeDebug.GetValueOnGameThread() >= 1)
		{
			UE_LOG(LogTemp, Warning, TEXT("[Kamikaze %s] Telegraph COMPLETE | Time=%.2fs Speed=%.0f -> CommitAttack"),
				*GetName(), StateTimer, VirtualVelocity.Size());
		}
		CommitAttack();
	}
}

void AKamikazeDroneNPC::UpdateAttacking(float DeltaTime)
{
	StateTimer += DeltaTime;

	// DEBUG: EMF force diagnostics 10x/sec during attack
	if (CVarKamikazeDebug.GetValueOnGameThread() >= 1)
	{
		static float NextLogTime = 0.f;
		const float Now = GetWorld()->GetTimeSeconds();
		if (Now >= NextLogTime)
		{
			NextLogTime = Now + 0.1f;
			const FVector EMFForce = EMFVelocityModifier ? EMFVelocityModifier->GetExternalForce_Implementation() : FVector::ZeroVector;
			const float Charge = EMFVelocityModifier ? EMFVelocityModifier->GetCharge() : 0.f;
			UCharacterMovementComponent* DbgCMC = GetCharacterMovement();
			UE_LOG(LogTemp, Warning, TEXT("[KAM_ATK %s] EMF Force=%s (%.0f) | Charge=%.1f | Vel=%.0f | MaxAccel=%.0f | Enabled=%d"),
				*GetName(), *EMFForce.ToCompactString(), EMFForce.Size(), Charge,
				GetVelocity().Size(),
				DbgCMC ? DbgCMC->MaxAcceleration : -1.f,
				EMFVelocityModifier ? (int32)EMFVelocityModifier->bEnabled : -1);
		}
	}

	// Move toward attack target with limited steering
	const FVector CurrentDir = GetVelocity().GetSafeNormal();
	const FVector DesiredDir = (AttackTargetPosition - GetActorLocation()).GetSafeNormal();

	// Limited turn rate during attack — steer toward target but DON'T overwrite AttackDirection
	const FVector SteeringDir = FMath::VInterpNormalRotationTo(CurrentDir, DesiredDir, DeltaTime, AttackTurnRate * AttackTurnRateMultiplier);

	if (UCharacterMovementComponent* CMC = GetCharacterMovement())
	{
		// --- FPV jitter: simulate construction imperfections, wind, PID noise ---
		const float T = GetWorld()->GetTimeSeconds() + SpeedNoiseTimeOffset;
		const FVector Right = FVector::CrossProduct(SteeringDir, FVector::UpVector).GetSafeNormal();
		const FVector Up = FVector::CrossProduct(Right, SteeringDir);
		const float JitterRight = FMath::Sin(T * 11.3f) * 0.6f + FMath::Sin(T * 7.1f) * 0.4f;
		const float JitterUp    = FMath::Sin(T * 9.7f) * 0.6f + FMath::Sin(T * 5.3f) * 0.4f;
		const FVector JitterOffset = (Right * JitterRight + Up * JitterUp) * AttackJitterAmplitude;
		const FVector NoisyDir = (SteeringDir + JitterOffset).GetSafeNormal();

		const float SpeedNoise = AttackSpeedJitter * FMath::Sin(T * 13.1f + 2.0f);
		CMC->MaxFlySpeed = AttackSpeed * (1.0f + SpeedNoise);
		CMC->AddInputVector(NoisyDir);

		// DEBUG: Jitter diagnostics 10x/sec
		if (CVarKamikazeDebug.GetValueOnGameThread() >= 1)
		{
			static float NextJitterLogTime = 0.f;
			if (T >= NextJitterLogTime)
			{
				NextJitterLogTime = T + 0.1f;
				const FVector ActualVel = GetVelocity();
				const float AngleDeg = FMath::RadiansToDegrees(FMath::Acos(FVector::DotProduct(ActualVel.GetSafeNormal(), SteeringDir)));
				UE_LOG(LogTemp, Warning, TEXT("[KAM_JIT %s] JitR=%.2f JitU=%.2f | NoisyAngle=%.1f° | Speed=%.0f/%.0f | Accel=%.0f"),
					*GetName(), JitterRight, JitterUp, AngleDeg,
					ActualVel.Size(), CMC->MaxFlySpeed, CMC->MaxAcceleration);
			}
		}
	}

	// --- Check player collision (sphere sweep from previous to current frame) ---
	if (CheckPlayerCollisionSweep())
	{
		return;
	}

	// --- Under-floor safety: if drone clipped through the floor, explode immediately ---
	// Flying mode CMC can fail to resolve floor collision at high speed (1200 cm/s),
	// allowing the drone to end up below the surface. Trace upward to detect this.
	{
		FHitResult FloorHit;
		FCollisionQueryParams FloorQuery;
		FloorQuery.AddIgnoredActor(this);
		const FVector Loc = GetActorLocation();
		if (GetWorld()->LineTraceSingleByChannel(FloorHit, Loc, Loc + FVector(0.0f, 0.0f, CollisionRadius + 10.0f), ECC_WorldStatic, FloorQuery))
		{
			if (CVarKamikazeDebug.GetValueOnGameThread() >= 1)
			{
				UE_LOG(LogTemp, Warning, TEXT("[Kamikaze %s] UNDER FLOOR detected | DroneZ=%.0f FloorZ=%.0f"),
					*GetName(), Loc.Z, FloorHit.Location.Z);
			}
			CurrentState = EKamikazeState::Dead;
			TriggerCollisionExplosion();
			KamikazeDie();
			return;
		}
	}

	// --- Sphere sweep forward for geometry (floor/wall) collision ---
	// Sweep (not raycast) so the drone's physical radius is accounted for —
	// a thin ray can slip between floor triangles or miss near-grazing surfaces,
	// while a sphere sweep reliably detects the floor the drone is about to hit.
	// 100 cm lookahead (~1 frame at AttackSpeed) keeps the drone from detonating
	// while still far from the surface.
	{
		FHitResult Hit;
		FCollisionQueryParams QueryParams;
		QueryParams.AddIgnoredActor(this);
		const FVector SweepStart = GetActorLocation();
		const FVector SweepEnd = SweepStart + GetVelocity().GetSafeNormal() * 100.0f;
		const FCollisionShape Sphere = FCollisionShape::MakeSphere(CollisionRadius);
		if (GetWorld()->SweepSingleByChannel(Hit, SweepStart, SweepEnd, FQuat::Identity, ECC_WorldStatic, Sphere, QueryParams))
		{
			if (CVarKamikazeDebug.GetValueOnGameThread() >= 1)
			{
				UE_LOG(LogTemp, Warning, TEXT("[Kamikaze %s] Attacking GEOMETRY IMPACT | Dist=%.0f HitActor=%s"),
					*GetName(), Hit.Distance, Hit.GetActor() ? *Hit.GetActor()->GetName() : TEXT("null"));
			}
			CurrentState = EKamikazeState::Dead;
			TriggerCollisionExplosion();
			KamikazeDie();
			return;
		}
	}

	// --- Check if we've passed the target ---
	// Velocity is set toward target in CommitAttack, so dot product with velocity is correct from frame 1
	const FVector ToTarget = AttackTargetPosition - GetActorLocation();
	const FVector TravelDir = GetVelocity().GetSafeNormal();
	const float DotToTarget = FVector::DotProduct(ToTarget, TravelDir);
	if (DotToTarget < 0.0f)
	{
		if (CVarKamikazeDebug.GetValueOnGameThread() >= 1)
		{
			UE_LOG(LogTemp, Warning, TEXT("[Kamikaze %s] PASSED TARGET | Dot=%.0f DistToTarget=%.0f -> PostAttack"),
				*GetName(), DotToTarget, ToTarget.Size());
		}
		// Use current velocity direction for PostAttack inertia
		AttackDirection = TravelDir;
		SetState(EKamikazeState::PostAttack);
	}
}

void AKamikazeDroneNPC::UpdatePostAttack(float DeltaTime)
{
	StateTimer += DeltaTime;

	// Continue at attack speed along current direction (with jitter)
	if (UCharacterMovementComponent* CMC = GetCharacterMovement())
	{
		const float T = GetWorld()->GetTimeSeconds() + SpeedNoiseTimeOffset;
		const FVector Right = FVector::CrossProduct(AttackDirection, FVector::UpVector).GetSafeNormal();
		const FVector Up = FVector::CrossProduct(Right, AttackDirection);
		const float JitterRight = FMath::Sin(T * 11.3f) * 0.6f + FMath::Sin(T * 7.1f) * 0.4f;
		const float JitterUp    = FMath::Sin(T * 9.7f) * 0.6f + FMath::Sin(T * 5.3f) * 0.4f;
		const FVector JitterOffset = (Right * JitterRight + Up * JitterUp) * AttackJitterAmplitude;
		const FVector NoisyDir = (AttackDirection + JitterOffset).GetSafeNormal();

		const float SpeedNoise = AttackSpeedJitter * FMath::Sin(T * 13.1f + 2.0f);
		CMC->MaxFlySpeed = AttackSpeed * (1.0f + SpeedNoise);
		CMC->AddInputVector(NoisyDir);
	}

	// --- Check player collision during post-attack inertia too ---
	if (CheckPlayerCollisionSweep())
	{
		return;
	}

	// --- Under-floor safety (same as Attacking) ---
	{
		FHitResult FloorHit;
		FCollisionQueryParams FloorQuery;
		FloorQuery.AddIgnoredActor(this);
		const FVector Loc = GetActorLocation();
		if (GetWorld()->LineTraceSingleByChannel(FloorHit, Loc, Loc + FVector(0.0f, 0.0f, CollisionRadius + 10.0f), ECC_WorldStatic, FloorQuery))
		{
			if (CVarKamikazeDebug.GetValueOnGameThread() >= 1)
			{
				UE_LOG(LogTemp, Warning, TEXT("[Kamikaze %s] PostAttack UNDER FLOOR detected | DroneZ=%.0f FloorZ=%.0f"),
					*GetName(), Loc.Z, FloorHit.Location.Z);
			}
			CurrentState = EKamikazeState::Dead;
			TriggerCollisionExplosion();
			KamikazeDie();
			return;
		}
	}

	// Grace period: skip geometry sweep for first 0.15s of PostAttack.
	// Without this, the drone arrives at the target point (where the player was), the floor is
	// just beneath the diving trajectory, and the sweep immediately fires -> premature explosion.
	if (StateTimer >= 0.15f)
	{
		// Sphere sweep forward for imminent geometry impact (100cm ahead)
		FHitResult Hit;
		FCollisionQueryParams QueryParams;
		QueryParams.AddIgnoredActor(this);

		const FVector SweepStart = GetActorLocation();
		const FVector SweepEnd = SweepStart + AttackDirection * 100.0f;
		const FCollisionShape Sphere = FCollisionShape::MakeSphere(CollisionRadius);

		if (GetWorld()->SweepSingleByChannel(Hit, SweepStart, SweepEnd, FQuat::Identity, ECC_WorldStatic, Sphere, QueryParams))
		{
			if (CVarKamikazeDebug.GetValueOnGameThread() >= 1)
			{
				UE_LOG(LogTemp, Warning, TEXT("[Kamikaze %s] PostAttack GEOMETRY IMPACT | Actor=%s Comp=%s Dist=%.0f HitLoc=(%.0f,%.0f,%.0f) Dir=(%.2f,%.2f,%.2f)"),
					*GetName(),
					Hit.GetActor() ? *Hit.GetActor()->GetName() : TEXT("null"),
					Hit.GetComponent() ? *Hit.GetComponent()->GetName() : TEXT("null"),
					Hit.Distance,
					Hit.Location.X, Hit.Location.Y, Hit.Location.Z,
					AttackDirection.X, AttackDirection.Y, AttackDirection.Z);
			}
			// Physical impact into wall/floor — FULL explosion, same as hitting the player
			// "Crash" (reduced) is only for being shot down in the air
			CurrentState = EKamikazeState::Dead;
			TriggerCollisionExplosion();
			KamikazeDie();
			return;
		}

#if ENABLE_DRAW_DEBUG
		if (CVarKamikazeDebug.GetValueOnGameThread() >= 2)
		{
			// Draw the forward sweep (orange = no hit)
			DrawDebugLine(GetWorld(), SweepStart, SweepEnd, FColor::Orange, false, 0.0f, 0, 1.5f);
		}
#endif
	}

	// After inertia time: roll crash chance or recover
	if (StateTimer >= PostAttackInertiaTime)
	{
		// Calculate crash chance
		const float Speed = GetVelocity().Size();
		const float SpeedFactor = FMath::Clamp((Speed - CruiseSpeed) / AttackSpeed, 0.0f, 1.0f) * SpeedCrashFactor;

		float PitchAngle = 0.0f;
		if (FPVTilt)
		{
			// Use the current mesh pitch as proxy
			PitchAngle = DroneMesh ? FMath::Abs(DroneMesh->GetRelativeRotation().Pitch) : 0.0f;
		}
		const float AngleFactor = (PitchAngle / 90.0f) * AngleCrashFactor;

		const float CrashChance = BaseCrashChance + SpeedFactor + AngleFactor;

		if (CVarKamikazeDebug.GetValueOnGameThread() >= 1)
		{
			UE_LOG(LogTemp, Warning, TEXT("[Kamikaze %s] PostAttack CRASH ROLL | Chance=%.2f (base=%.2f + speed=%.2f + angle=%.2f) Speed=%.0f Pitch=%.0f"),
				*GetName(), CrashChance, BaseCrashChance, SpeedFactor, AngleFactor, Speed, PitchAngle);
		}

		if (InstanceRandom.FRand() < CrashChance)
		{
			if (CVarKamikazeDebug.GetValueOnGameThread() >= 1)
			{
				UE_LOG(LogTemp, Warning, TEXT("[Kamikaze %s] PostAttack RANDOM CRASH (rolled under %.2f)"), *GetName(), CrashChance);
			}
			// Random crash — lost control — set Dead before KamikazeDie to prevent double dispatch
			CurrentState = EKamikazeState::Dead;
			TriggerCrashExplosion();
			KamikazeDie();
			return;
		}

		// Recovery
		SetState(EKamikazeState::Recovery);
	}
}

void AKamikazeDroneNPC::UpdateRecovery(float DeltaTime)
{
	StateTimer += DeltaTime;

	// Decelerate and turn back toward orbit
	if (UCharacterMovementComponent* CMC = GetCharacterMovement())
	{
		// First frame of Recovery: restore capsule blocking pawns (was Overlap during attack)
		if (StateTimer <= DeltaTime + SMALL_NUMBER)
		{
			GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
		}

		// Restore normal acceleration after attack's boosted value
		CMC->MaxAcceleration = 2048.0f;

		// Gradually reduce speed back to cruise
		const float TargetSpeed = FMath::FInterpTo(CMC->MaxFlySpeed, CruiseSpeed, DeltaTime, 3.0f);
		CMC->MaxFlySpeed = TargetSpeed;

		// Turn toward orbit altitude above player (not just player position)
		if (APawn* Player = GetPlayerPawn())
		{
			const FVector MyLoc = GetActorLocation();
			// Recovery target: player XY but at orbit altitude
			const FVector PlayerPos = Player->GetActorLocation();
			const FVector RecoveryTarget(PlayerPos.X, PlayerPos.Y, PlayerPos.Z + OrbitBaseHeight);
			const float HeightError = RecoveryTarget.Z - MyLoc.Z;

			// Horizontal: toward player
			const FVector HorizTarget(PlayerPos.X, PlayerPos.Y, MyLoc.Z);
			const FVector HorizDir = (HorizTarget - MyLoc).GetSafeNormal();

			// Strong vertical correction during recovery — drone must regain altitude
			const float VerticalUrgency = FMath::Clamp(FMath::Abs(HeightError) / 150.0f, 0.0f, 1.0f);
			const float VerticalComponent = FMath::Sign(HeightError) * VerticalUrgency * 0.8f;

			const FVector RecoveryDir = (HorizDir + FVector(0.0f, 0.0f, VerticalComponent)).GetSafeNormal();

			const float T = GetWorld()->GetTimeSeconds() + SpeedNoiseTimeOffset;
			const FVector Right = FVector::CrossProduct(RecoveryDir, FVector::UpVector).GetSafeNormal();
			const FVector Up = FVector::CrossProduct(Right, RecoveryDir);
			const float JitterRight = FMath::Sin(T * 11.3f) * 0.6f + FMath::Sin(T * 7.1f) * 0.4f;
			const float JitterUp    = FMath::Sin(T * 9.7f) * 0.6f + FMath::Sin(T * 5.3f) * 0.4f;
			const FVector NoisyDir = (RecoveryDir + (Right * JitterRight + Up * JitterUp) * AttackJitterAmplitude).GetSafeNormal();
			CMC->AddInputVector(NoisyDir);

			// --- Recovery height diagnostics (every 0.5s) ---
			if (CVarKamikazeDebug.GetValueOnGameThread() >= 1)
			{
				const float LogInterval = 0.5f;
				if (FMath::FloorToInt(StateTimer / LogInterval) != FMath::FloorToInt((StateTimer - DeltaTime) / LogInterval))
				{
					UE_LOG(LogTemp, Warning,
						TEXT("[KAM_RECOVERY %s] t=%.1f | DroneZ=%.0f TargetZ=%.0f Err=%.0f | VelZ=%.0f Speed=%.0f MaxFly=%.0f | VertUrg=%.2f VertComp=%.2f | InputDir=(%.2f,%.2f,%.2f)"),
						*GetName(), StateTimer, MyLoc.Z, RecoveryTarget.Z, HeightError,
						GetVelocity().Z, GetVelocity().Size(), CMC->MaxFlySpeed,
						VerticalUrgency, VerticalComponent,
						NoisyDir.X, NoisyDir.Y, NoisyDir.Z);
				}
			}
		}
	}

	// Recovery takes ~2 seconds, then return to orbit
	if (StateTimer >= 2.0f)
	{
		// Reset orbit and strafe
		CurrentOrbitRadius = OrbitStartRadius;
		OrbitForcedTimer = 0.0f;
		bOrbitForced = false;
		bIsRetaliating = false;
		bIsStrafing = false;
		ProximityTimer = 0.0f;
		StrafeCumulativePhase = 0.0f;
		OrbitEvaluationTimer = 0.0f;
		LastBlockedRayCount = 0;

		// Recalculate orbit angle based on current position relative to orbit center
		const FVector Offset = GetActorLocation() - OrbitCenter;
		OrbitAngle = FMath::Atan2(Offset.Y, Offset.X);
		OrbitCumulativeAngle = 0.0f;

		SetState(EKamikazeState::Orbiting);
	}
}

// ==================== Parry ====================

void AKamikazeDroneNPC::InitiateParry(AController* AttackerController)
{
	bIsParried = true;

	// Cancel any knockback in progress
	if (bIsInKnockback)
	{
		EndKnockbackStun();
	}

	// Stop CMC entirely — we drive position directly via SetActorLocation
	if (UCharacterMovementComponent* CMC = GetCharacterMovement())
	{
		CMC->StopMovementImmediately();
		CMC->DisableMovement();  // MOVE_None — CMC won't interfere
	}

	// Disable collision with player during parry flight
	if (ACharacter* PlayerChar = UGameplayStatics::GetPlayerCharacter(this, 0))
	{
		MoveIgnoreActorAdd(PlayerChar);
	}

	// Get player look direction
	FVector PlayerLoc = FVector::ZeroVector;
	FVector PlayerForward = FVector::ForwardVector;
	if (AttackerController)
	{
		if (APawn* AttackerPawn = AttackerController->GetPawn())
		{
			PlayerLoc = AttackerPawn->GetActorLocation();
			PlayerForward = AttackerPawn->GetControlRotation().Vector();
		}
	}

	// Find redirect target in cone
	ParryTarget = FindParryTarget(PlayerLoc, PlayerForward);

	if (ParryTarget.IsValid())
	{
		// Aim at enemy
		ParryDirection = (ParryTarget->GetActorLocation() - GetActorLocation()).GetSafeNormal();
	}
	else
	{
		// Project a point far along player look direction, then aim drone at it.
		// This ensures the drone flies "where the player is looking" regardless of
		// where the drone currently is relative to the player.
		const FVector LookTarget = PlayerLoc + PlayerForward * 3000.0f;
		ParryDirection = (LookTarget - GetActorLocation()).GetSafeNormal();

		// Ensure slight downward bias for eventual ground impact
		if (ParryDirection.Z > -0.05f)
		{
			ParryDirection.Z = -0.05f;
			ParryDirection.Normalize();
		}
	}

	// Build orthonormal basis for spiral
	ParrySpiralRight = FVector::CrossProduct(ParryDirection, FVector::UpVector).GetSafeNormal();
	if (ParrySpiralRight.IsNearlyZero())
	{
		ParrySpiralRight = FVector::CrossProduct(ParryDirection, FVector::RightVector).GetSafeNormal();
	}
	ParrySpiralUp = FVector::CrossProduct(ParrySpiralRight, ParryDirection).GetSafeNormal();

	ParrySpiralAngle = 0.0f;
	ParryCurrentRadius = ParrySpiralStartRadius;

	// Disable capsule collision — we handle collision manually via sweeps
	// This prevents CMC from blocking our direct position updates
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Overlap);
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Overlap);

	// Reset PreviousFrameLocation so the sweep in first UpdateParried frame
	// doesn't trace from some old position and instantly hit geometry
	PreviousFrameLocation = GetActorLocation();

	SetState(EKamikazeState::Parried);
}

AShooterNPC* AKamikazeDroneNPC::FindParryTarget(const FVector& PlayerLocation, const FVector& PlayerForward) const
{
	const float CosHalfAngle = FMath::Cos(FMath::DegreesToRadians(ParryConeLookHalfAngle));
	AShooterNPC* BestTarget = nullptr;
	float BestDistSq = ParryConeLookDistance * ParryConeLookDistance;

	// Overlap sphere to find candidates
	TArray<FOverlapResult> Overlaps;
	FCollisionShape Sphere = FCollisionShape::MakeSphere(ParryConeLookDistance);
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);
	if (APawn* Player = UGameplayStatics::GetPlayerPawn(GetWorld(), 0))
	{
		QueryParams.AddIgnoredActor(Player);
	}

	GetWorld()->OverlapMultiByChannel(Overlaps, PlayerLocation, FQuat::Identity, ECC_Pawn, Sphere, QueryParams);

	for (const FOverlapResult& Overlap : Overlaps)
	{
		AShooterNPC* NPC = Cast<AShooterNPC>(Overlap.GetActor());
		if (!NPC || NPC == this || NPC->IsDead())
		{
			continue;
		}

		// Cone check from player position
		const FVector ToTarget = NPC->GetActorLocation() - PlayerLocation;
		const float DistSq = ToTarget.SizeSquared();
		const FVector DirToTarget = ToTarget.GetSafeNormal();
		const float CosAngle = FVector::DotProduct(PlayerForward, DirToTarget);

		if (CosAngle >= CosHalfAngle && DistSq < BestDistSq)
		{
			// LOS check
			FHitResult LOSHit;
			FCollisionQueryParams LOSParams;
			LOSParams.AddIgnoredActor(this);
			LOSParams.AddIgnoredActor(NPC);
			const bool bBlocked = GetWorld()->LineTraceSingleByChannel(
				LOSHit, PlayerLocation, NPC->GetActorLocation(), ECC_Visibility, LOSParams);
			if (!bBlocked)
			{
				BestTarget = NPC;
				BestDistSq = DistSq;
			}
		}
	}

	return BestTarget;
}

void AKamikazeDroneNPC::UpdateParried(float DeltaTime)
{
	StateTimer += DeltaTime;

	// Timeout — explode in place
	if (StateTimer >= ParryMaxFlightTime)
	{
		DoExplosion(ParryExplosionRadius, ParryExplosionDamage, UDamageType_KamikazeExplosion::StaticClass(), true);
		KamikazeDie();
		return;
	}

	// If we have a target, update direction toward it (homing)
	if (ParryTarget.IsValid())
	{
		AShooterNPC* TargetNPC = Cast<AShooterNPC>(ParryTarget.Get());
		if (TargetNPC && !TargetNPC->IsDead())
		{
			const FVector ToTarget = TargetNPC->GetActorLocation() - GetActorLocation();
			const float DistToTarget = ToTarget.Size();

			if (DistToTarget > 10.0f)
			{
				ParryDirection = ToTarget / DistToTarget;

				// Rebuild basis
				ParrySpiralRight = FVector::CrossProduct(ParryDirection, FVector::UpVector).GetSafeNormal();
				if (ParrySpiralRight.IsNearlyZero())
				{
					ParrySpiralRight = FVector::CrossProduct(ParryDirection, FVector::RightVector).GetSafeNormal();
				}
				ParrySpiralUp = FVector::CrossProduct(ParrySpiralRight, ParryDirection).GetSafeNormal();
			}

			// Close enough — explode
			if (DistToTarget < CollisionRadius + 60.0f)
			{
				DoExplosion(ParryExplosionRadius, ParryExplosionDamage, UDamageType_KamikazeExplosion::StaticClass(), true);
				KamikazeDie();
				return;
			}
		}
		else
		{
			// Target died — keep flying current direction
			ParryTarget.Reset();
		}
	}

	// Update spiral angle and radius
	ParrySpiralAngle += ParrySpiralAngularSpeed * DeltaTime;
	ParryCurrentRadius = FMath::Max(0.0f, ParryCurrentRadius - ParrySpiralRadiusShrinkRate * DeltaTime);

	// Calculate spiral offset perpendicular to flight direction
	const FVector SpiralOffset = (ParrySpiralRight * FMath::Cos(ParrySpiralAngle) + ParrySpiralUp * FMath::Sin(ParrySpiralAngle)) * ParryCurrentRadius;

	// Gravity bias (only when no target — makes drone spiral into ground)
	FVector GravityComponent = FVector::ZeroVector;
	if (!ParryTarget.IsValid())
	{
		GravityComponent = FVector(0.0f, 0.0f, -ParryGravityBias * DeltaTime);
	}

	// Calculate new position
	const FVector OldLocation = GetActorLocation();
	const FVector ForwardMove = ParryDirection * ParrySpiralForwardSpeed * DeltaTime;
	const FVector CenterLineTarget = OldLocation + ForwardMove + GravityComponent;
	const FVector FinalTarget = CenterLineTarget + SpiralOffset;

	// --- Geometry collision check BEFORE moving (sweep from old to new) ---
	{
		FHitResult Hit;
		FCollisionQueryParams QueryParams;
		QueryParams.AddIgnoredActor(this);
		if (GetWorld()->SweepSingleByChannel(Hit, OldLocation, FinalTarget, FQuat::Identity,
			ECC_WorldStatic, FCollisionShape::MakeSphere(CollisionRadius * 0.5f), QueryParams))
		{
			// Move to impact point, then explode
			SetActorLocation(Hit.Location, false);
			DoExplosion(ParryExplosionRadius, ParryExplosionDamage, UDamageType_KamikazeExplosion::StaticClass(), true);
			KamikazeDie();
			return;
		}
	}

	// --- NPC collision check at target position ---
	{
		TArray<FOverlapResult> Overlaps;
		FCollisionShape Sphere = FCollisionShape::MakeSphere(CollisionRadius + 20.0f);
		FCollisionQueryParams QueryParams;
		QueryParams.AddIgnoredActor(this);

		GetWorld()->OverlapMultiByChannel(Overlaps, FinalTarget, FQuat::Identity, ECC_Pawn, Sphere, QueryParams);
		for (const FOverlapResult& Overlap : Overlaps)
		{
			AShooterNPC* NPC = Cast<AShooterNPC>(Overlap.GetActor());
			if (NPC && !NPC->IsDead() && NPC != this)
			{
				SetActorLocation(FinalTarget, false);
				DoExplosion(ParryExplosionRadius, ParryExplosionDamage, UDamageType_KamikazeExplosion::StaticClass(), true);
				KamikazeDie();
				return;
			}
		}
	}

	// --- Move drone (no sweep — we already checked collisions above) ---
	SetActorLocation(FinalTarget, false);

	// Store velocity for debug/orientation
	if (UCharacterMovementComponent* CMC = GetCharacterMovement())
	{
		CMC->Velocity = (FinalTarget - OldLocation) / FMath::Max(DeltaTime, KINDA_SMALL_NUMBER);
	}


	// Spin the mesh for visual tumble effect
	if (DroneMesh)
	{
		const float SpinDelta = ParryMeshSpinSpeed * DeltaTime;
		DroneMesh->AddLocalRotation(FRotator(SpinDelta * 0.7f, SpinDelta, SpinDelta * 0.3f));
	}
}

// ==================== Orbit Quality Evaluation ====================

void AKamikazeDroneNPC::EvaluateOrbitQuality()
{
	APawn* Player = GetPlayerPawn();
	if (!Player) return;

	const FVector Center = Player->GetActorLocation();
	const float Radius = CurrentOrbitRadius;
	int32 BlockedCount = 0;

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);
	QueryParams.AddIgnoredActor(Player);

	for (int32 i = 0; i < 8; ++i)
	{
		const float Angle = (UE_TWO_PI / 8.0f) * i;
		const FVector SamplePos = Center + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f) * Radius;

		FHitResult Hit;
		const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, Center, SamplePos, ECC_WorldStatic, QueryParams);

		if (bHit)
		{
			++BlockedCount;
		}

		// Debug: draw evaluation rays
		if (CVarKamikazeDebug.GetValueOnGameThread() >= 2)
		{
			DrawDebugLine(GetWorld(), Center, bHit ? Hit.ImpactPoint : SamplePos,
				bHit ? FColor::Red : FColor::Green, false, 1.0f, 0, 1.0f);
		}
	}

	LastBlockedRayCount = BlockedCount;

	if (!bIsStrafing && BlockedCount >= BadOrbitThreshold)
	{
		// Switch to strafe
		bIsStrafing = true;
		StrafePhase = 0.0f;
		StrafeCumulativePhase = 0.0f;

		// Request strafe slot from coordinator
		AAICombatCoordinator* Coordinator = AAICombatCoordinator::GetCoordinator(this);
		if (Coordinator)
		{
			Coordinator->RequestStrafeSlot(this, CurrentOrbitRadius, StrafeCenter, StrafeAxis);
		}
		else
		{
			// Fallback: strafe perpendicular to player direction at current position
			const FVector ToPlayer = FVector(Player->GetActorLocation().X - GetActorLocation().X,
				Player->GetActorLocation().Y - GetActorLocation().Y, 0.0f).GetSafeNormal();
			StrafeAxis = FVector::CrossProduct(FVector::UpVector, ToPlayer).GetSafeNormal();
			StrafeCenter = GetActorLocation();
		}

		if (CVarKamikazeDebug.GetValueOnGameThread() >= 1)
		{
			UE_LOG(LogTemp, Warning, TEXT("[Kamikaze %s] Orbit BAD (%d/8 blocked) → STRAFE"), *GetName(), BlockedCount);
		}
	}
}

// ==================== Strafe Movement ====================

void AKamikazeDroneNPC::UpdateStrafing(float DeltaTime)
{
	APawn* Player = GetPlayerPawn();
	if (!Player) return;

	OrbitElapsedTime += DeltaTime;

	// --- Evaluate orbit quality (once per second) ---
	OrbitEvaluationTimer += DeltaTime;
	if (OrbitEvaluationTimer >= 1.0f)
	{
		OrbitEvaluationTimer = 0.0f;
		EvaluateOrbitQuality();

		// Re-request strafe slot (coordinator redistributes)
		if (bIsStrafing)
		{
			AAICombatCoordinator* Coordinator = AAICombatCoordinator::GetCoordinator(this);
			if (Coordinator)
			{
				Coordinator->RequestStrafeSlot(this, CurrentOrbitRadius, StrafeCenter, StrafeAxis);
			}
		}
	}

	// --- Transition back to orbit when orbit clears ---
	if (LastBlockedRayCount < BadOrbitThreshold)
	{
		const float PrevSin = FMath::Sin(StrafePhase);
		StrafePhase += StrafeFrequency * DeltaTime;
		const float CurrSin = FMath::Sin(StrafePhase);
		if (PrevSin * CurrSin <= 0.0f) // zero crossing
		{
			bIsStrafing = false;
			const FVector Offset = GetActorLocation() - OrbitCenter;
			OrbitAngle = FMath::Atan2(Offset.Y, Offset.X);
			OrbitCumulativeAngle = 0.0f;

			// Release strafe slot
			if (AAICombatCoordinator* Coord = AAICombatCoordinator::GetCoordinator(this))
			{
				Coord->ReleaseStrafeSlot(this);
			}

			if (CVarKamikazeDebug.GetValueOnGameThread() >= 1)
			{
				UE_LOG(LogTemp, Warning, TEXT("[Kamikaze %s] Orbit CLEAR (%d/8 blocked) → ORBIT"), *GetName(), LastBlockedRayCount);
			}
			return;
		}
	}
	else
	{
		StrafePhase += StrafeFrequency * DeltaTime;
	}

	// --- Lap counting: full sin period = 2PI = one "lap" ---
	StrafeCumulativePhase += StrafeFrequency * DeltaTime;
	if (StrafeCumulativePhase >= UE_TWO_PI)
	{
		StrafeCumulativePhase -= UE_TWO_PI;
		CurrentOrbitRadius = FMath::Max(CurrentOrbitRadius - OrbitShrinkPerLap, OrbitMinRadius);
	}

	// --- Compute strafe position ---
	const float T = GetWorld()->GetTimeSeconds() + SpeedNoiseTimeOffset;
	const float Noise = SpeedNoiseAmplitude * FMath::Sin(T * 3.7f);
	const float LateralOffset = StrafeAmplitude * FMath::Sin(StrafePhase + Noise);

	const FVector PlayerPos = Player->GetActorLocation();
	const FVector ToCenter = StrafeCenter - PlayerPos;
	const FVector CenterDir = FVector(ToCenter.X, ToCenter.Y, 0.0f).GetSafeNormal();
	const FVector BasePos = PlayerPos + CenterDir * CurrentOrbitRadius;
	const FVector TargetPosXY = BasePos + StrafeAxis * LateralOffset;

	// Height: same logic as orbit
	const float HeightOsc = OrbitHeightAmplitude * FMath::Sin(StrafePhase + OrbitHeightPhaseOffset);
	const float TargetZ = PlayerPos.Z + OrbitBaseHeight + HeightOsc;
	const FVector TargetPos(TargetPosXY.X, TargetPosXY.Y, TargetZ);

	// --- Move toward target (separate horizontal/vertical control) ---
	if (UCharacterMovementComponent* CMC = GetCharacterMovement())
	{
		const FVector MyLoc = GetActorLocation();
		const float HeightError = TargetZ - MyLoc.Z;

		const FVector HorizTarget(TargetPos.X, TargetPos.Y, MyLoc.Z);
		const FVector HorizDir = (HorizTarget - MyLoc).GetSafeNormal();

		const float VerticalUrgency = FMath::Clamp(FMath::Abs(HeightError) / 200.0f, 0.0f, 1.0f);
		const float VerticalComponent = FMath::Sign(HeightError) * VerticalUrgency * 0.6f;

		FVector MoveDir = (HorizDir + FVector(0.0f, 0.0f, VerticalComponent)).GetSafeNormal();

		// FPV jitter (reuse existing pattern)
		const FVector Right = FVector::CrossProduct(MoveDir, FVector::UpVector).GetSafeNormal();
		const FVector Up = FVector::CrossProduct(Right, MoveDir);
		const float JitterRight = FMath::Sin(T * 11.3f) * 0.6f + FMath::Sin(T * 7.1f) * 0.4f;
		const float JitterUp    = FMath::Sin(T * 9.7f) * 0.6f + FMath::Sin(T * 5.3f) * 0.4f;
		const FVector NoisyDir = (MoveDir + (Right * JitterRight + Up * JitterUp) * AttackJitterAmplitude).GetSafeNormal();

		const float SpeedNoise = SpeedNoiseAmplitude * FMath::Sin(T * 2.7f);
		CMC->MaxFlySpeed = CruiseSpeed * (1.0f + SpeedNoise);
		CMC->AddInputVector(NoisyDir);
	}

	// Debug: draw strafe info
	if (CVarKamikazeDebug.GetValueOnGameThread() >= 2)
	{
		DrawDebugSphere(GetWorld(), StrafeCenter, 20.0f, 6, FColor::Orange, false, 0.0f);
		DrawDebugLine(GetWorld(), StrafeCenter - StrafeAxis * StrafeAmplitude,
			StrafeCenter + StrafeAxis * StrafeAmplitude, FColor::Orange, false, 0.0f, 0, 2.0f);
		DrawDebugSphere(GetWorld(), TargetPos, 15.0f, 4, FColor::Cyan, false, 0.0f);
	}

	// --- Token-based attack check (same as orbit) ---
	if (OrbitElapsedTime >= MinOrbitTimeBeforeAttack)
	{
		AAICombatCoordinator* Coordinator = AAICombatCoordinator::GetCoordinator(this);
		if (Coordinator && Coordinator->RequestAttackToken(this, EAttackTokenType::Kamikaze))
		{
			bIsRetaliating = false;
			SetState(EKamikazeState::Positioning);
			return;
		}
	}

	// --- Proximity attack (same as orbit) ---
	const float DistToPlayer = FVector::Dist(GetActorLocation(), PlayerPos);
	if (CurrentOrbitRadius <= ProximityAttackRadius)
	{
		ProximityTimer += DeltaTime;
		if (ProximityTimer >= ProximityAttackDelay)
		{
			// Emergency — skip positioning
			BeginTelegraph(false);
			return;
		}
	}
	else
	{
		ProximityTimer = 0.0f;
	}
}

// ==================== Attack ====================

void AKamikazeDroneNPC::BeginTelegraph(bool bRetaliation)
{
	if (CurrentState != EKamikazeState::Orbiting && CurrentState != EKamikazeState::Positioning)
	{
		return;
	}

	if (CVarKamikazeDebug.GetValueOnGameThread() >= 1)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Kamikaze %s] BeginTelegraph | Retaliation=%d OrbitForced=%d OrbitTime=%.1f"),
			*GetName(), bRetaliation, bOrbitForced, OrbitElapsedTime);
	}

	bIsRetaliating = bRetaliation;

	// --- Snapshot state BEFORE disabling CMC ---
	TelegraphStartPos = GetActorLocation();
	TelegraphPrevPos = TelegraphStartPos;
	const FVector OrbitTangent = GetVelocity().GetSafeNormal(); // actual flight direction = orbit tangent

	// Attack direction toward predicted player position
	AttackTargetPosition = CalculatePredictedPosition();
	TelegraphAttackDir = (AttackTargetPosition - TelegraphStartPos).GetSafeNormal();

	// --- Cubic Bezier control points (repurpose phantom variables) ---
	// P0 = TelegraphStartPos
	// P1 = TelegraphPhantomOrbitCenter  (orbit tangent pull — big initial curve)
	// P2 = computed from P3 - AttackDir * TelegraphPhantomOrbitSpeed (small end curve)
	// P3 = TelegraphStartPos + AttackDir * TelegraphPhantomOrbitAngle
	const float ArcLength = CruiseSpeed * TelegraphDuration;
	TelegraphPhantomOrbitCenter = TelegraphStartPos + OrbitTangent * ArcLength * 0.5f;   // P1
	TelegraphPhantomOrbitAngle = ArcLength * 0.5f;                                       // P3 distance
	TelegraphPhantomOrbitSpeed = ArcLength * 0.15f;                                      // P2 pull-back

	// Disable CMC movement — we position the drone manually via SetActorLocation
	if (UCharacterMovementComponent* CMC = GetCharacterMovement())
	{
		CMC->StopMovementImmediately();
		CMC->SetMovementMode(MOVE_None);
	}

	SetState(EKamikazeState::Telegraphing);

	// Play telegraph sound
	if (TelegraphSound)
	{
		UGameplayStatics::PlaySoundAtLocation(GetWorld(), TelegraphSound, GetActorLocation());
	}
}

void AKamikazeDroneNPC::CommitAttack()
{
	// Recalculate predicted target position fresh (player may have moved during telegraph)
	AttackTargetPosition = CalculatePredictedPosition();
	AttackDirection = (AttackTargetPosition - GetActorLocation()).GetSafeNormal();

	// Restore CMC flying mode (was MOVE_None during phantom lerp) and snap velocity
	if (UCharacterMovementComponent* CMC = GetCharacterMovement())
	{
		CMC->SetMovementMode(MOVE_Flying);
		CMC->Velocity = AttackDirection * AttackSpeed;
		CMC->MaxFlySpeed = AttackSpeed;
		CMC->MaxAcceleration = 5000.0f;
	}

	// Capsule Block→Overlap on Pawn during dive: CMC physics resolves Block collision
	// BEFORE Actor::Tick, pushing the drone away from the player. CheckPlayerCollisionSweep
	// then sees post-deflection positions and misses. With Overlap the drone flies through
	// the player capsule and the sweep reliably catches intersection → explosion.
	// WorldStatic stays Block so drone still collides with floors/walls.
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);

	if (CVarKamikazeDebug.GetValueOnGameThread() >= 1)
	{
		const float DistToTarget = FVector::Dist(GetActorLocation(), AttackTargetPosition);
		UE_LOG(LogTemp, Warning, TEXT("[Kamikaze %s] CommitAttack | Target=(%.0f,%.0f,%.0f) Dist=%.0f Dir=(%.2f,%.2f,%.2f)"),
			*GetName(),
			AttackTargetPosition.X, AttackTargetPosition.Y, AttackTargetPosition.Z,
			DistToTarget, AttackDirection.X, AttackDirection.Y, AttackDirection.Z);
	}

	SetState(EKamikazeState::Attacking);
}

bool AKamikazeDroneNPC::CheckPlayerCollisionSweep()
{
	APawn* Player = GetPlayerPawn();
	if (!Player) return false;

	const FVector CurrentLoc = GetActorLocation();
	const FVector SweepStart = PreviousFrameLocation;
	const FVector SweepEnd = CurrentLoc;

	// Skip if barely moved (orbiting, idle)
	if (FVector::DistSquared(SweepStart, SweepEnd) < 1.0f) return false;

	// Sphere sweep from previous position to current
	// Drone collision radius + player capsule radius (~34cm) for reliable detection
	const float SweepRadius = CollisionRadius + 34.0f;

	FHitResult Hit;
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	const FCollisionShape SweepSphere = FCollisionShape::MakeSphere(SweepRadius);

	const bool bHit = GetWorld()->SweepSingleByChannel(
		Hit, SweepStart, SweepEnd, FQuat::Identity,
		ECC_Pawn, SweepSphere, QueryParams);

	if (bHit && Hit.GetActor() == Player)
	{
		if (CVarKamikazeDebug.GetValueOnGameThread() >= 1)
		{
			UE_LOG(LogTemp, Warning, TEXT("[Kamikaze %s] SWEEP HIT PLAYER | SweepDist=%.0f HitDist=%.0f Radius=%.0f State=%s"),
				*GetName(), FVector::Dist(SweepStart, SweepEnd), Hit.Distance, SweepRadius,
				KamikazeStateToString(CurrentState));
		}
		CurrentState = EKamikazeState::Dead;
		TriggerCollisionExplosion();
		KamikazeDie();
		return true;
	}

	return false;
}

FVector AKamikazeDroneNPC::CalculatePredictedPosition() const
{
	APawn* Player = GetPlayerPawn();
	if (!Player)
	{
		return GetActorLocation() + GetActorForwardVector() * 500.0f;
	}

	// Aim at upper chest level (30cm below actor origin, which is capsule center)
	const FVector PlayerTarget = Player->GetActorLocation() - FVector(0.0f, 0.0f, 30.0f);

	if (PredictionOrder == 0)
	{
		// Zero order: aim at current position
		return PlayerTarget;
	}

	// First order: pos + vel * timeToImpact
	const float Distance = FVector::Dist(GetActorLocation(), PlayerTarget);
	const float TimeToImpact = Distance / FMath::Max(AttackSpeed, 1.0f);
	const FVector PlayerVel = Player->GetVelocity();

	FVector Predicted = PlayerTarget + PlayerVel * TimeToImpact;

	// Clamp predicted position above the floor so the drone doesn't dive underground
	// (happens when player has negative Z velocity — falling, stepping off edges, etc.)
	FHitResult FloorHit;
	FCollisionQueryParams FloorQuery;
	FloorQuery.AddIgnoredActor(this);
	FloorQuery.AddIgnoredActor(Player);
	const FVector FloorTraceStart = FVector(Predicted.X, Predicted.Y, PlayerTarget.Z + 100.0f);
	const FVector FloorTraceEnd = FVector(Predicted.X, Predicted.Y, Predicted.Z - 500.0f);
	if (GetWorld()->LineTraceSingleByChannel(FloorHit, FloorTraceStart, FloorTraceEnd, ECC_WorldStatic, FloorQuery))
	{
		// Don't aim below the floor — clamp to floor + half capsule height
		const float MinZ = FloorHit.Location.Z + CollisionRadius;
		if (Predicted.Z < MinZ)
		{
			Predicted.Z = MinZ;
		}
	}

	return Predicted;
}

// ==================== Damage ====================

float AKamikazeDroneNPC::TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (bIsDead)
	{
		return 0.0f;
	}

	// Mark damage for StateTree
	bTookDamageThisFrame = true;
	LastDamageTakenTime = GetWorld()->GetTimeSeconds();

	// Friendly fire filter (same pattern as FlyingDrone)
	if (DamageCauser)
	{
		bool bIsCollisionDamage = DamageEvent.DamageTypeClass &&
			(DamageEvent.DamageTypeClass->IsChildOf(UDamageType_Wallslam::StaticClass()) ||
			 DamageEvent.DamageTypeClass->IsChildOf(UDamageType_EMFProximity::StaticClass()));

		if (!bIsCollisionDamage)
		{
			AActor* DamageOwner = DamageCauser->GetOwner();
			if (Cast<AShooterNPC>(DamageCauser) || Cast<AShooterNPC>(DamageOwner))
			{
				return 0.0f;
			}
		}

		if (EventInstigator)
		{
			if (Cast<AShooterNPC>(EventInstigator->GetPawn()))
			{
				return 0.0f;
			}
		}
	}

	// Reduce HP
	CurrentHP -= Damage;

	// Handle melee: parry instead of charge transfer
	const bool bIsMeleeDamage = DamageEvent.DamageTypeClass && DamageEvent.DamageTypeClass->IsChildOf(UDamageType_Melee::StaticClass());
	if (bIsMeleeDamage)
	{
		// Charge transfer
		if (EMFVelocityModifier && EventInstigator)
		{
			APawn* Attacker = EventInstigator->GetPawn();
			if (Attacker)
			{
				UEMFVelocityModifier* AttackerEMF = Attacker->FindComponentByClass<UEMFVelocityModifier>();
				float ChargeToAdd = ChargeChangeOnMeleeHit;

				if (AttackerEMF)
				{
					float AttackerCharge = AttackerEMF->GetCharge();
					ChargeToAdd = -FMath::Abs(ChargeChangeOnMeleeHit) * FMath::Sign(AttackerCharge);
					if (FMath::Abs(AttackerCharge) < KINDA_SMALL_NUMBER)
					{
						ChargeToAdd = ChargeChangeOnMeleeHit;
					}
				}

				float OldCharge = EMFVelocityModifier->GetCharge();
				EMFVelocityModifier->SetCharge(OldCharge + ChargeToAdd);
			}
		}

		// Parry: prevent death from this hit, enter parried state
		if (CurrentState != EKamikazeState::Parried && CurrentState != EKamikazeState::Dead)
		{
			CurrentHP = FMath::Max(CurrentHP, 1.0f); // Don't let melee kill — force parry
			InitiateParry(EventInstigator);
			return Damage;
		}
	}

	// Broadcast damage event
	FVector HitLocation = GetActorLocation() + FVector(0.0f, 0.0f, 30.0f);
	OnDamageTaken.Broadcast(this, Damage, DamageEvent.DamageTypeClass, HitLocation, DamageCauser);

	// Retaliation: if hit during orbit → position for frontal attack
	if (bRetaliateOnDamage && CurrentState == EKamikazeState::Orbiting)
	{
		bIsRetaliating = true;
		SetState(EKamikazeState::Positioning);
	}

	// Check for death
	if (CurrentHP <= 0.0f)
	{
		LastKillingDamageType = DamageEvent.DamageTypeClass;
		LastKillingDamageCauser = DamageCauser;

		if (DamageEvent.IsOfType(FRadialDamageEvent::ClassID))
		{
			const FRadialDamageEvent& RadialEvent = static_cast<const FRadialDamageEvent&>(DamageEvent);
			LastKillingHitDirection = (GetActorLocation() - RadialEvent.Origin).GetSafeNormal();
		}
		else if (DamageCauser)
		{
			LastKillingHitDirection = (GetActorLocation() - DamageCauser->GetActorLocation()).GetSafeNormal();
		}

		KamikazeDie();
	}

	return Damage;
}

// ==================== EMF Capture ====================

void AKamikazeDroneNPC::EnterCapturedState(UAnimMontage* OverrideMontage)
{
	// Save state so we know what was interrupted
	StateBeforeCapture = CurrentState;

	// Stop CMC — drone hangs in the air held by EMF forces
	if (UCharacterMovementComponent* CMC = GetCharacterMovement())
	{
		CMC->StopMovementImmediately();
		CMC->SetMovementMode(MOVE_None);
	}

	// Let base class handle bIsCaptured, bIsInKnockback, montage, etc.
	Super::EnterCapturedState(OverrideMontage);
}

void AKamikazeDroneNPC::ExitCapturedState()
{
	// Let base class handle cleanup first
	Super::ExitCapturedState();

	// Drone explodes on release — it's a kamikaze, no point returning to orbit
	TriggerCollisionExplosion();
	KamikazeDie();
}

// ==================== Death ====================

void AKamikazeDroneNPC::KamikazeDie()
{
	if (bIsDead || bDeathSequenceStarted)
	{
		return;
	}

	bDeathSequenceStarted = true;
	bIsDead = true;

	// Stop shooting (no weapon, but base class safety)
	StopShooting();

	// Unregister from coordinator
	UnregisterFromCoordinator();

	// Increment score
	if (AShooterGameMode* GM = Cast<AShooterGameMode>(GetWorld()->GetAuthGameMode()))
	{
		GM->IncrementTeamScore(TeamByte);
	}

	// Broadcast death
	OnNPCDeath.Broadcast(this);
	OnNPCDeathDetailed.Broadcast(this, LastKillingDamageType, LastKillingDamageCauser);

	if (CVarKamikazeDebug.GetValueOnGameThread() >= 1)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Kamikaze %s] KamikazeDie | DeathState=%s Pos=(%.0f,%.0f,%.0f) Vel=%.0f"),
			*GetName(), KamikazeStateToString(CurrentState),
			GetActorLocation().X, GetActorLocation().Y, GetActorLocation().Z,
			GetVelocity().Size());
	}

	// Death behavior depends on state at death
	switch (CurrentState)
	{
	case EKamikazeState::Launching:
	case EKamikazeState::Orbiting:
	case EKamikazeState::Recovery:
		// Killed on orbit/launch — debris fall, no explosion, YES HP pickup
		TriggerDebrisFall();
		break;

	case EKamikazeState::Positioning:
	case EKamikazeState::Telegraphing:
	case EKamikazeState::Attacking:
	{
		// Killed during attack — check distance to player
		if (APawn* Player = GetPlayerPawn())
		{
			const float DistToPlayer = FVector::Dist(GetActorLocation(), Player->GetActorLocation());
			if (DistToPlayer <= AttackDeathDistanceThreshold)
			{
				// Close to player — air explosion
				TriggerAirExplosion();
				break;
			}
		}
		// Far from player — just debris
		TriggerDebrisFall();
		break;
	}

	case EKamikazeState::PostAttack:
		// Killed by player during post-attack — debris fall
		TriggerDebrisFall();
		break;

	case EKamikazeState::Parried:
		// Parried drone — explosion already handled in UpdateParried, just debris
		TriggerDebrisFall();
		break;

	case EKamikazeState::Dead:
		// Already handled externally (crash/collision explosion set state to Dead before calling KamikazeDie)
		break;

	default:
		TriggerDebrisFall();
		break;
	}

	// Spawn death gibs
	if (DeathGeometryCollection)
	{
		const FDeathModeConfig& DeathConfig = ResolveDeathConfig();
		SpawnDeathGeometryCollection(DeathConfig);
	}

	// Aggressive deactivation
	DeactivateAllSystems();

	// Schedule destruction
	GetWorld()->GetTimerManager().SetTimer(
		DeathSequenceTimer, this, &AKamikazeDroneNPC::DeathDestroy, 0.5f, false);
}

void AKamikazeDroneNPC::TriggerDebrisFall()
{
	if (CVarKamikazeDebug.GetValueOnGameThread() >= 1)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Kamikaze %s] >>> TriggerDebrisFall (NO explosion, YES HP)"), *GetName());
	}
	// No explosion — just enable gravity so debris/mesh falls
	if (UCharacterMovementComponent* CMC = GetCharacterMovement())
	{
		CMC->GravityScale = 1.0f;
		CMC->SetMovementMode(MOVE_Falling);
	}

	// Drop HP pickups (reward for killing)
	if (HealthPickupClass)
	{
		AHealthPickup::SpawnHealthPickups(GetWorld(), HealthPickupClass, GetActorLocation(),
			HealthPickupDropCount, HealthPickupScatterRadius, HealthPickupFloorOffset);
	}
}

void AKamikazeDroneNPC::TriggerAirExplosion()
{
	if (CVarKamikazeDebug.GetValueOnGameThread() >= 1)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Kamikaze %s] >>> TriggerAirExplosion (crash radius, YES HP)"), *GetName());
	}
	// Smaller explosion when killed during attack approach
	DoExplosion(CrashExplosionRadius, CrashDamage, UDamageType_KamikazeExplosion::StaticClass(), true);
}

void AKamikazeDroneNPC::TriggerCrashExplosion()
{
	if (CVarKamikazeDebug.GetValueOnGameThread() >= 1)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Kamikaze %s] >>> TriggerCrashExplosion (crash radius, NO HP) | Pos=(%.0f,%.0f,%.0f)"),
			*GetName(), GetActorLocation().X, GetActorLocation().Y, GetActorLocation().Z);
	}
	// Crash into geometry — explosion but NO HP pickup
	DoExplosion(CrashExplosionRadius, CrashDamage, UDamageType_KamikazeExplosion::StaticClass(), false);

	// Play crash sound
	if (CrashSound)
	{
		UGameplayStatics::PlaySoundAtLocation(GetWorld(), CrashSound, GetActorLocation());
	}
}

void AKamikazeDroneNPC::TriggerCollisionExplosion()
{
	if (CVarKamikazeDebug.GetValueOnGameThread() >= 1)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Kamikaze %s] >>> TriggerCollisionExplosion (FULL radius, YES HP) | Pos=(%.0f,%.0f,%.0f)"),
			*GetName(), GetActorLocation().X, GetActorLocation().Y, GetActorLocation().Z);
	}
	// Direct hit on player — full explosion
	DoExplosion(ExplosionRadius, CollisionDamage, UDamageType_KamikazeExplosion::StaticClass(), true);
}

void AKamikazeDroneNPC::DoExplosion(float Radius, float Damage, TSubclassOf<UDamageType> DamageTypeClass, bool bDropHealthPickup)
{
	// Spawn VFX
	UNiagaraSystem* FXToUse = (Radius >= ExplosionRadius) ? ExplosionFX : CrashExplosionFX;
	if (FXToUse)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), FXToUse, GetActorLocation());
	}

	// Play sound
	if (ExplosionSound)
	{
		UGameplayStatics::PlaySoundAtLocation(GetWorld(), ExplosionSound, GetActorLocation());
	}

	// Apply radial damage (same manual overlap pattern as FlyingDrone::TriggerExplosion)
	if (Damage > 0.0f && Radius > 0.0f)
	{
		const FVector Origin = GetActorLocation();

		TArray<FOverlapResult> Overlaps;
		FCollisionShape Sphere = FCollisionShape::MakeSphere(Radius);
		FCollisionQueryParams QueryParams;
		QueryParams.AddIgnoredActor(this);

		GetWorld()->OverlapMultiByChannel(Overlaps, Origin, FQuat::Identity, ECC_Pawn, Sphere, QueryParams);

		// Use PlayerPawn as DamageCauser to bypass NPC friendly-fire and show damage numbers
		APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);

		TSet<AActor*> DamagedActors;

		for (const FOverlapResult& Overlap : Overlaps)
		{
			AActor* HitActor = Overlap.GetActor();
			if (!HitActor || DamagedActors.Contains(HitActor))
			{
				continue;
			}
			DamagedActors.Add(HitActor);

			// LOS check
			FHitResult LOSHit;
			FCollisionQueryParams LOSParams;
			LOSParams.AddIgnoredActor(this);
			LOSParams.AddIgnoredActor(HitActor);
			const bool bBlocked = GetWorld()->LineTraceSingleByChannel(
				LOSHit, Origin, HitActor->GetActorLocation(), ECC_Visibility, LOSParams);
			if (bBlocked)
			{
				continue;
			}

			// Linear falloff
			const float Distance = FVector::Dist(Origin, HitActor->GetActorLocation());
			const float DamageScale = FMath::Clamp(1.0f - Distance / Radius, 0.0f, 1.0f);
			const float FinalDamage = Damage * DamageScale;

			if (FinalDamage <= 0.0f)
			{
				continue;
			}

			FRadialDamageEvent RadialDamageEvent;
			RadialDamageEvent.DamageTypeClass = DamageTypeClass;
			RadialDamageEvent.Origin = Origin;
			RadialDamageEvent.Params.BaseDamage = Damage;
			RadialDamageEvent.Params.OuterRadius = Radius;
			HitActor->TakeDamage(FinalDamage, RadialDamageEvent, nullptr, PlayerPawn);
		}
	}

	// Stun nearby NPCs — only when drone was parried by melee
	if (Radius > 0.0f && bIsParried)
	{
		TArray<FOverlapResult> StunOverlaps;
		FCollisionShape StunSphere = FCollisionShape::MakeSphere(Radius);
		FCollisionQueryParams StunQueryParams;
		StunQueryParams.AddIgnoredActor(this);

		GetWorld()->OverlapMultiByChannel(
			StunOverlaps, GetActorLocation(), FQuat::Identity,
			ECC_Pawn, StunSphere, StunQueryParams);

		TSet<AShooterNPC*> StunnedNPCs;

		for (const FOverlapResult& Overlap : StunOverlaps)
		{
			AShooterNPC* NPC = Cast<AShooterNPC>(Overlap.GetActor());
			if (!NPC || StunnedNPCs.Contains(NPC) || NPC->IsDead() || NPC == this)
			{
				continue;
			}
			StunnedNPCs.Add(NPC);
			NPC->ApplyExplosionStun(ExplosionStunDuration, ExplosionStunMontage);
		}
	}

	// Drop HP pickups
	if (bDropHealthPickup && HealthPickupClass)
	{
		AHealthPickup::SpawnHealthPickups(GetWorld(), HealthPickupClass, GetActorLocation(),
			HealthPickupDropCount, HealthPickupScatterRadius, HealthPickupFloorOffset);
	}

	// Hide mesh (explosion replaces it)
	if (DroneMesh)
	{
		DroneMesh->SetVisibility(false);
	}
}

void AKamikazeDroneNPC::DeactivateAllSystems()
{
	// Disable collision
	if (DroneCollision)
	{
		DroneCollision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Disable movement
	if (FlyingMovement)
	{
		FlyingMovement->SetComponentTickEnabled(false);
	}
	if (UCharacterMovementComponent* CMC = GetCharacterMovement())
	{
		CMC->StopMovementImmediately();
		CMC->DisableMovement();
		CMC->SetComponentTickEnabled(false);
	}

	// Hide mesh
	if (DroneMesh)
	{
		DroneMesh->SetVisibility(false);
		DroneMesh->SetComponentTickEnabled(false);
	}

	// Stop flight sound
	if (FlightAudioComponent)
	{
		FlightAudioComponent->FadeOut(0.3f, 0.0f);
	}

	// Disable EMF
	if (EMFVelocityModifier)
	{
		EMFVelocityModifier->SetCharge(0.0f);
		EMFVelocityModifier->SetComponentTickEnabled(false);
	}
	if (FieldComponent)
	{
		FieldComponent->UnregisterFromRegistry();
		FieldComponent->SetComponentTickEnabled(false);
	}

	// Disable accuracy component (kamikaze doesn't use it, but inherited from ShooterNPC)
	// AccuracyComponent is not used by kamikaze drone — skip

	// Disable FPV tilt
	if (FPVTilt)
	{
		FPVTilt->SetComponentTickEnabled(false);
	}

	// Unpossess
	if (AController* MyController = GetController())
	{
		MyController->UnPossess();
	}

	// Disable actor tick
	SetActorTickEnabled(false);
}

void AKamikazeDroneNPC::DeathDestroy()
{
	Destroy();
}

// ==================== Knockback Overrides (FlyingDrone pattern) ====================

void AKamikazeDroneNPC::ApplyKnockback(const FVector& InKnockbackDirection, float Distance, float Duration, const FVector& AttackerLocation, bool bKeepEMFEnabled)
{
	// Don't apply knockback if parried — we control movement ourselves
	if (bIsInKnockback || bIsParried)
	{
		return;
	}

	// Stop FlyingMovement
	if (FlyingMovement)
	{
		FlyingMovement->StopMovement();
	}

	// Ignore player collision during knockback
	if (!AttackerLocation.IsZero())
	{
		if (ACharacter* PlayerChar = UGameplayStatics::GetPlayerCharacter(this, 0))
		{
			float DistToAttacker = FVector::Dist(PlayerChar->GetActorLocation(), AttackerLocation);
			if (DistToAttacker < 300.0f)
			{
				KnockbackIgnoreActor = PlayerChar;
				MoveIgnoreActorAdd(PlayerChar);
			}
		}
	}

	Super::ApplyKnockback(InKnockbackDirection, Distance, Duration, AttackerLocation, bKeepEMFEnabled);
}

void AKamikazeDroneNPC::EndKnockbackStun()
{
	// Restore player collision
	if (KnockbackIgnoreActor.IsValid())
	{
		MoveIgnoreActorRemove(KnockbackIgnoreActor.Get());
		KnockbackIgnoreActor.Reset();
	}

	Super::EndKnockbackStun();

	// Restore flying mode
	if (UCharacterMovementComponent* CMC = GetCharacterMovement())
	{
		CMC->SetMovementMode(MOVE_Flying);
	}
}

void AKamikazeDroneNPC::OnCapsuleHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	// Block parent's OnCapsuleHit during knockback interpolation (same as FlyingDrone)
	if (bIsKnockbackInterpolating)
	{
		return;
	}

	Super::OnCapsuleHit(HitComponent, OtherActor, OtherComp, NormalImpulse, Hit);
}

void AKamikazeDroneNPC::SpawnDeathGeometryCollection(const FDeathModeConfig& Config)
{
	if (!DeathGeometryCollection || !GetWorld())
	{
		return;
	}

	// Use DroneMesh transform instead of SkeletalMesh
	const FTransform MeshTransform = DroneMesh ? DroneMesh->GetComponentTransform()
		: FTransform(GetActorLocation());
	const FVector Origin = MeshTransform.GetLocation();

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AGeometryCollectionActor* GCActor = GetWorld()->SpawnActor<AGeometryCollectionActor>(
		Origin, MeshTransform.GetRotation().Rotator(), SpawnParams);

	if (!GCActor)
	{
		return;
	}

	UGeometryCollectionComponent* GCComp = GCActor->GetGeometryCollectionComponent();
	if (!GCComp)
	{
		GCActor->Destroy();
		return;
	}

	// Scale GC to match drone visual mesh
	if (DroneMesh)
	{
		GCActor->SetActorScale3D(DroneMesh->GetComponentScale());
	}

	// Collision: gibs collide with world but not pawns/camera
	GCComp->SetCollisionProfileName(FName("Ragdoll"));
	GCComp->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	GCComp->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	GCComp->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);

	GCComp->SetRestCollection(DeathGeometryCollection);

	// Copy materials from DroneMesh to GC gibs
	if (DroneMesh)
	{
		const int32 NumMats = DroneMesh->GetNumMaterials();
		for (int32 i = 0; i < NumMats; i++)
		{
			if (UMaterialInterface* Mat = DroneMesh->GetMaterial(i))
			{
				GCComp->SetMaterial(i, Mat);
			}
		}
	}

	GCComp->SetSimulatePhysics(true);
	GCComp->RecreatePhysicsState();

	// Break all clusters
	UUniformScalar* StrainField = NewObject<UUniformScalar>(GCActor);
	StrainField->Magnitude = 999999.0f;
	GCComp->ApplyPhysicsField(true,
		EGeometryCollectionPhysicsTypeEnum::Chaos_ExternalClusterStrain,
		nullptr, StrainField);

	// Scatter pieces radially
	URadialVector* RadialVelocity = NewObject<URadialVector>(GCActor);
	RadialVelocity->Magnitude = Config.DismembermentImpulse;
	RadialVelocity->Position = Origin;
	GCComp->ApplyPhysicsField(true,
		EGeometryCollectionPhysicsTypeEnum::Chaos_LinearVelocity,
		nullptr, RadialVelocity);

	// Angular velocity for tumbling
	URadialVector* AngularVel = NewObject<URadialVector>(GCActor);
	AngularVel->Magnitude = Config.DismembermentAngularImpulse;
	AngularVel->Position = Origin;
	GCComp->ApplyPhysicsField(true,
		EGeometryCollectionPhysicsTypeEnum::Chaos_AngularVelocity,
		nullptr, AngularVel);

	// Directional bias from killing hit direction
	if (!LastKillingHitDirection.IsNearlyZero() && Config.DirectionalBiasMultiplier > 0.0f)
	{
		UUniformVector* DirectionalBias = NewObject<UUniformVector>(GCActor);
		DirectionalBias->Magnitude = Config.DismembermentImpulse * Config.DirectionalBiasMultiplier;
		DirectionalBias->Direction = LastKillingHitDirection;
		GCComp->ApplyPhysicsField(true,
			EGeometryCollectionPhysicsTypeEnum::Chaos_LinearVelocity,
			nullptr, DirectionalBias);
	}

	// Auto-destroy gibs
	GCActor->SetLifeSpan(GibLifetime);
}

// ==================== Public Queries ====================

bool AKamikazeDroneNPC::TookDamageRecently(float GracePeriod) const
{
	return (GetWorld()->GetTimeSeconds() - LastDamageTakenTime) <= GracePeriod;
}

bool AKamikazeDroneNPC::IsInAttackSequence() const
{
	return CurrentState == EKamikazeState::Positioning
		|| CurrentState == EKamikazeState::Telegraphing
		|| CurrentState == EKamikazeState::Attacking
		|| CurrentState == EKamikazeState::PostAttack
		|| CurrentState == EKamikazeState::Parried;
}

// ==================== Helpers ====================

APawn* AKamikazeDroneNPC::GetPlayerPawn() const
{
	return UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
}
