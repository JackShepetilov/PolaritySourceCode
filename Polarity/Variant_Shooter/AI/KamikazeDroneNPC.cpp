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
	case EKamikazeState::Orbiting:     return TEXT("Orbiting");
	case EKamikazeState::Telegraphing: return TEXT("Telegraphing");
	case EKamikazeState::Attacking:    return TEXT("Attacking");
	case EKamikazeState::PostAttack:   return TEXT("PostAttack");
	case EKamikazeState::Recovery:     return TEXT("Recovery");
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

	// Update collision sizes
	if (DroneCollision)
	{
		DroneCollision->SetSphereRadius(CollisionRadius);
	}
	GetCapsuleComponent()->SetCapsuleSize(CollisionRadius, CollisionRadius);

	// Update mesh scale
	if (DroneMesh && DroneMesh->GetStaticMesh())
	{
		const float MeshScale = (CollisionRadius * 2.0f) / 100.0f;
		DroneMesh->SetRelativeScale3D(FVector(MeshScale));
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
}

void AKamikazeDroneNPC::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (bIsDead)
	{
		return;
	}

	// Reset per-frame flags
	bTookDamageThisFrame = false;

	// State machine
	switch (CurrentState)
	{
	case EKamikazeState::Orbiting:
		UpdateOrbiting(DeltaTime);
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
	default:
		break;
	}

	// Update FPV tilt every frame
	if (FPVTilt && !bIsDead)
	{
		const FVector Vel = GetVelocity();
		const float Speed = Vel.Size();
		// Approximate acceleration from velocity change (CMC doesn't expose it directly)
		const FVector Accel = GetCharacterMovement() ? GetCharacterMovement()->GetCurrentAcceleration() : FVector::ZeroVector;
		FPVTilt->SetMovementState(Speed, Vel, Accel);
	}

	// Orient actor to face velocity direction (yaw only)
	const FVector Vel = GetVelocity();
	if (!Vel.IsNearlyZero(10.0f))
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

void AKamikazeDroneNPC::UpdateOrbiting(float DeltaTime)
{
	APawn* Player = GetPlayerPawn();
	if (!Player)
	{
		return;
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

	// --- Move drone toward orbit position ---
	const FVector MoveDir = (TargetOrbitPos - GetActorLocation()).GetSafeNormal();
	if (UCharacterMovementComponent* CMC = GetCharacterMovement())
	{
		CMC->AddInputVector(MoveDir);
		CMC->MaxFlySpeed = EffectiveSpeed;
	}

	// --- Token-based attack check ---
	OrbitElapsedTime += DeltaTime;
	if (OrbitElapsedTime >= MinOrbitTimeBeforeAttack)
	{
		AAICombatCoordinator* Coordinator = AAICombatCoordinator::GetCoordinator(this);
		if (Coordinator && Coordinator->RequestAttackToken(this, EAttackTokenType::Kamikaze))
		{
			BeginTelegraph(false);
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
			// Emergency attack — stuck at close range too long
			BeginTelegraph(false);
			return;
		}
	}
	else
	{
		ProximityTimer = 0.0f;
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
	const FVector BezierPos = u*u*u * P0 + 3.0f*u*u*t * P1 + 3.0f*u*t*t * P2 + t*t*t * P3;

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
		const FVector NoisyDir = (SteeringDir + (Right * JitterRight + Up * JitterUp) * AttackJitterAmplitude).GetSafeNormal();

		const float SpeedNoise = AttackSpeedJitter * FMath::Sin(T * 13.1f + 2.0f);
		CMC->MaxFlySpeed = AttackSpeed * (1.0f + SpeedNoise);
		CMC->AddInputVector(NoisyDir);
	}

	// --- Check player collision (sphere sweep from previous to current frame) ---
	if (CheckPlayerCollisionSweep())
	{
		return;
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
		const FVector NoisyDir = (AttackDirection + (Right * JitterRight + Up * JitterUp) * AttackJitterAmplitude).GetSafeNormal();

		const float SpeedNoise = AttackSpeedJitter * FMath::Sin(T * 13.1f + 2.0f);
		CMC->MaxFlySpeed = AttackSpeed * (1.0f + SpeedNoise);
		CMC->AddInputVector(NoisyDir);
	}

	// --- Check player collision during post-attack inertia too ---
	if (CheckPlayerCollisionSweep())
	{
		return;
	}

	// Grace period: skip raycast for first 0.15s of PostAttack.
	// Without this, the drone arrives at the target point (where the player was), the floor is
	// just beneath the diving trajectory, and the raycast immediately fires -> premature explosion.
	if (StateTimer >= 0.15f)
	{
		// Raycast forward for imminent geometry impact (200cm ahead)
		FHitResult Hit;
		FCollisionQueryParams QueryParams;
		QueryParams.AddIgnoredActor(this);

		const FVector RayStart = GetActorLocation();
		const FVector RayEnd = RayStart + AttackDirection * 200.0f;

		if (GetWorld()->LineTraceSingleByChannel(Hit, RayStart, RayEnd, ECC_WorldStatic, QueryParams))
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
			// Draw the forward raycast (orange = no hit)
			DrawDebugLine(GetWorld(), RayStart, RayEnd, FColor::Orange, false, 0.0f, 0, 1.5f);
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
		// Gradually reduce speed back to cruise
		const float TargetSpeed = FMath::FInterpTo(CMC->MaxFlySpeed, CruiseSpeed, DeltaTime, 3.0f);
		CMC->MaxFlySpeed = TargetSpeed;

		// Turn toward orbit center
		if (APawn* Player = GetPlayerPawn())
		{
			const FVector ToPlayer = (Player->GetActorLocation() - GetActorLocation()).GetSafeNormal();
			CMC->AddInputVector(ToPlayer);
		}
	}

	// Recovery takes ~2 seconds, then return to orbit
	if (StateTimer >= 2.0f)
	{
		// Reset orbit
		CurrentOrbitRadius = OrbitStartRadius;
		OrbitForcedTimer = 0.0f;
		bOrbitForced = false;
		bIsRetaliating = false;
		ProximityTimer = 0.0f;

		// Recalculate orbit angle based on current position relative to orbit center
		const FVector Offset = GetActorLocation() - OrbitCenter;
		OrbitAngle = FMath::Atan2(Offset.Y, Offset.X);
		OrbitCumulativeAngle = 0.0f;

		SetState(EKamikazeState::Orbiting);
	}
}

// ==================== Attack ====================

void AKamikazeDroneNPC::BeginTelegraph(bool bRetaliation)
{
	if (CurrentState != EKamikazeState::Orbiting)
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
	}

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

	const FVector PlayerPos = Player->GetActorLocation();

	if (PredictionOrder == 0)
	{
		// Zero order: aim at current position
		return PlayerPos;
	}

	// First order: pos + vel * timeToImpact
	const float Distance = FVector::Dist(GetActorLocation(), PlayerPos);
	const float TimeToImpact = Distance / FMath::Max(AttackSpeed, 1.0f);
	const FVector PlayerVel = Player->GetVelocity();

	return PlayerPos + PlayerVel * TimeToImpact;
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

	// Handle melee charge transfer
	if (DamageEvent.DamageTypeClass && DamageEvent.DamageTypeClass->IsChildOf(UDamageType_Melee::StaticClass()))
	{
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
	}

	// Broadcast damage event
	FVector HitLocation = GetActorLocation() + FVector(0.0f, 0.0f, 30.0f);
	OnDamageTaken.Broadcast(this, Damage, DamageEvent.DamageTypeClass, HitLocation, DamageCauser);

	// Retaliation: if hit during orbit → immediate attack
	if (bRetaliateOnDamage && CurrentState == EKamikazeState::Orbiting)
	{
		BeginTelegraph(true);
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
	case EKamikazeState::Orbiting:
	case EKamikazeState::Recovery:
		// Killed on orbit — debris fall, no explosion, YES HP pickup
		TriggerDebrisFall();
		break;

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

	// Stun nearby NPCs
	if (Radius > 0.0f)
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
	if (bIsInKnockback)
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
	return CurrentState == EKamikazeState::Telegraphing
		|| CurrentState == EKamikazeState::Attacking
		|| CurrentState == EKamikazeState::PostAttack;
}

// ==================== Helpers ====================

APawn* AKamikazeDroneNPC::GetPlayerPawn() const
{
	return UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
}
