// KamikazeDroneNPC.cpp

#include "KamikazeDroneNPC.h"
#include "KamikazeStrikeSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Variant_Shooter/Weapons/ShooterWeapon_Melee.h"
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
#include "AI/PolarityTeams.h"
#include "../DamageTypes/DamageType_Melee.h"
#include "../DamageTypes/DamageType_Wallslam.h"
#include "../DamageTypes/DamageType_EMFProximity.h"
#include "../DamageTypes/DamageType_KamikazeExplosion.h"
#include "../Pickups/HealthPickup.h"
#include "ShooterGameMode.h"
#include "AICombatCoordinator.h"
#include "ShooterCharacter.h"
#include "DrawDebugHelpers.h"
#include "Variant_Shooter/Buildables/BuildableActor.h"

namespace
{
	/** "This target is no longer worth flying at". Death is spelled differently on the two pawn
	 *  families (AShooterNPC::IsDead, AShooterCharacter::IsDead) and neither is virtual on the
	 *  shared base, so the question is asked here once instead of at every call site. */
	bool IsTargetGone(const APawn* Pawn)
	{
		if (!IsValid(Pawn))
		{
			return true;
		}
		if (const AShooterNPC* const NPC = Cast<AShooterNPC>(Pawn))
		{
			return NPC->IsDead();
		}
		if (const AShooterCharacter* const Player = Cast<AShooterCharacter>(Pawn))
		{
			return Player->IsDead();
		}
		return false;
	}
}

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
	case EKamikazeState::Orbiting:     return TEXT("Loitering");
	case EKamikazeState::Attacking:    return TEXT("Attacking");
	case EKamikazeState::PostAttack:   return TEXT("PostAttack");
	case EKamikazeState::Recovery:     return TEXT("PullUp");
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

// polarity.debug.novfx: та же глушилка, что у оружия. Гасится только картинка - урон, звук и
// смерть идут как обычно, иначе отладочный флаг менял бы исход боя.
static bool KamikazeNoVFX()
{
	static IConsoleVariable* const Var =
		IConsoleManager::Get().FindConsoleVariable(TEXT("polarity.debug.novfx"));
	return Var && Var->GetInt() != 0;
}

namespace
{
	/** Move Current toward Desired by at most MaxDelta: an acceleration limit on a velocity. */
	FVector StepVelocityToward(const FVector& Current, const FVector& Desired, float MaxDelta)
	{
		const FVector Diff = Desired - Current;
		const float Len = Diff.Size();
		if (Len <= MaxDelta || Len < KINDA_SMALL_NUMBER)
		{
			return Desired;
		}
		return Current + Diff / Len * MaxDelta;
	}

	/** Within this of the hold point the drone counts as holding (cm). */
	constexpr float HoldArriveTolerance = 200.0f;

	/** A recovery that cannot get back to the hold point (blocked, target moving away) gives up
	 *  after this long and holds from wherever it is (seconds). */
	constexpr float RecoveryMaxTime = 4.0f;

	/** After the wind-up, this long to swing the nose round at low speed before the run counts
	 *  (seconds). 180 degrees at the low-speed turn rate of 720/s is a quarter of a second. */
	constexpr float StrikeTurnOutWindow = 0.4f;
}

void AKamikazeDroneNPC::BeginPlay()
{
	Super::BeginPlay();

	// NOTE: Collision sizes, profiles, and mesh scale are configured in Blueprint.
	// Do NOT override them here — BeginPlay overrides break Blueprint collision setup.

	// FlyingAIMovementComponent's tick would overwrite MaxFlySpeed every frame; only its BeginPlay
	// (the CMC setup) is wanted here.
	if (FlyingMovement)
	{
		FlyingMovement->SetComponentTickEnabled(false);
	}

	if (FPVTilt)
	{
		FPVTilt->Initialize(DroneMesh, StrikeSpeed, GetUniqueID());
	}

	CurrentState = EKamikazeState::Orbiting;
	PreviousFrameLocation = GetActorLocation();
	LastVelocity = GetVelocity();

	// Spread the drift phase by instance, so a ring of drones does not sway in step.
	DriftPhase = static_cast<float>(GetUniqueID() % 1000) * 0.001f * UE_TWO_PI;
	RepositionSide = (GetUniqueID() % 2) ? 1.0f : -1.0f;

	// Flight belongs to the authority: it moves the pawn by hand and the result replicates. A client
	// leaves its CMC alone, because that tick is what smooths the movement it receives.
	if (HasAuthority())
	{
		SetHandDrivenFlight(true);
		if (UKamikazeStrikeSubsystem* Queue = GetStrikeQueue())
		{
			Queue->Register(this);
		}
	}

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

	// Acceleration from the velocity change, lightly smoothed: it drives the bank into turns and the
	// motor pitch. Works on clients too, from the replicated velocity.
	{
		const FVector Vel = GetVelocity();
		const FVector RawAccel = (DeltaTime > KINDA_SMALL_NUMBER) ? (Vel - LastVelocity) / DeltaTime : FVector::ZeroVector;
		SmoothedAcceleration = FMath::VInterpTo(SmoothedAcceleration, RawAccel, DeltaTime, 12.0f);
		LastVelocity = Vel;
	}

	// Motor pitch follows thrust: high when fast, and high while speeding up, braking or turning hard.
	if (FlightAudioComponent && FlightAudioComponent->IsPlaying())
	{
		const float Speed = GetVelocity().Size();
		const float SpeedRange = FMath::Max(FlightPitchMaxSpeed - FlightPitchMinSpeed, 1.0f);
		const float SpeedAlpha = FMath::Clamp((Speed - FlightPitchMinSpeed) / SpeedRange, 0.0f, 1.0f);
		const float AccelAlpha = FMath::Clamp(SmoothedAcceleration.Size() / FlightPitchMaxAccel, 0.0f, 1.0f);
		FlightAudioComponent->SetPitchMultiplier(FMath::Lerp(FlightPitchMin, FlightPitchMax, FMath::Max(SpeedAlpha, AccelAlpha)));
	}

	// Reset per-frame flags
	bTookDamageThisFrame = false;

	// Who to fly at, from the strike queue: players shared out evenly. Null leaves GetTargetPawn to
	// its own logic (no live player, or a faction target).
	if (HasAuthority())
	{
		UKamikazeStrikeSubsystem* const Queue = GetStrikeQueue();
		APawn* const NewTarget = Queue ? Queue->GetAssignedTarget(this) : nullptr;
		if (NewTarget != QueueTarget.Get())
		{
			// Handed to another player: a strike granted against the old one is void.
			bStrikeGranted = false;
		}
		QueueTarget = NewTarget;
	}

	// Everything below moves the pawn or decides damage, and both belong to the authority. A client
	// sees the result through replicated movement; running the flight there as well would fly a second,
	// local copy of the drone and detonate it on the client. Captured and knocked-back drones are
	// driven by whoever captured or knocked them.
	if (HasAuthority() && !bIsCaptured && !bIsInKnockback)
	{
		switch (CurrentState)
		{
		case EKamikazeState::Launching:
			UpdateLaunching(DeltaTime);
			break;
		case EKamikazeState::Orbiting:
			UpdateHold(DeltaTime, true);
			break;
		case EKamikazeState::Recovery:
			UpdateHold(DeltaTime, false);
			break;
		case EKamikazeState::Attacking:
			UpdateStrike(DeltaTime);
			break;
		case EKamikazeState::Parried:
			UpdateParried(DeltaTime);
			break;
		default:
			break;
		}
	}

	// A state update may have just blown the drone up.
	if (bIsDead)
	{
		return;
	}

	// Update FPV tilt every frame (skip when parried — mesh spins freely): nose into speed, nose up
	// when braking, bank into turns from the sideways acceleration.
	if (FPVTilt && CurrentState != EKamikazeState::Parried)
	{
		const FVector Vel = GetVelocity();
		FPVTilt->SetMovementState(Vel.Size(), Vel, SmoothedAcceleration);
	}

	// Yaw only, and the authority's rotation replicates. Holding, the camera stays on the target the
	// way a pilot keeps it in the goggles; everywhere else the nose follows the flight.
	if (HasAuthority() && CurrentState != EKamikazeState::Parried)
	{
		const FVector Vel = GetVelocity();
		const APawn* const Target = (CurrentState == EKamikazeState::Orbiting) ? GetTargetPawn() : nullptr;
		const FVector Facing = Target ? (Target->GetActorLocation() - GetActorLocation()) : Vel;
		if (!Facing.IsNearlyZero(10.0f))
		{
			const FRotator NewRot = FMath::RInterpTo(GetActorRotation(), FRotator(0.0f, Facing.Rotation().Yaw, 0.0f), DeltaTime, 6.0f);
			SetActorRotation(NewRot);
		}
	}

	// Update previous location for sweep collision detection (must be last in Tick)
	PreviousFrameLocation = GetActorLocation();
}

// ==================== State Machine ====================

void AKamikazeDroneNPC::SetState(EKamikazeState NewState)
{
	const EKamikazeState OldState = CurrentState;
	CurrentState = NewState;
	StateTimer = 0.0f;

	// Every arrival at the hold starts the wait from zero: a strike is always announced by a full hold.
	if (NewState == EKamikazeState::Orbiting)
	{
		HoldTimer = 0.0f;
		bProximityTimedOut = false;
		bStrikeGranted = false;
		bIsRetaliating = false;
	}

	if (CVarKamikazeDebug.GetValueOnGameThread() >= 1)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Kamikaze %s] State: %s -> %s | Pos=(%.0f,%.0f,%.0f) Vel=%.0f"),
			*GetName(), KamikazeStateToString(OldState), KamikazeStateToString(NewState),
			GetActorLocation().X, GetActorLocation().Y, GetActorLocation().Z,
			GetVelocity().Size());
	}
}

// ==================== Flight ====================

void AKamikazeDroneNPC::SetHandDrivenFlight(bool bHandDriven)
{
	UCharacterMovementComponent* CMC = GetCharacterMovement();
	if (!CMC)
	{
		return;
	}

	// Flying mode either way: it is what collision and replicated movement expect of a pawn in the
	// air, and gravity stays off. Only the tick changes hands.
	CMC->SetMovementMode(MOVE_Flying);
	CMC->SetComponentTickEnabled(!bHandDriven);
}

FHitResult AKamikazeDroneNPC::FlyMove(const FVector& NewVelocity, float DeltaTime)
{
	FHitResult FirstHit;
	UCharacterMovementComponent* CMC = GetCharacterMovement();
	if (!CMC || DeltaTime <= 0.0f)
	{
		return FirstHit;
	}

	CMC->Velocity = NewVelocity;
	const FVector Delta = NewVelocity * DeltaTime;
	if (Delta.IsNearlyZero())
	{
		return FirstHit;
	}

	// SafeMove, not a plain move: a drone that starts inside geometry (a carrier hanging too low, a
	// spawn point in a wall) is pushed out first instead of sweeping from inside and passing through.
	CMC->SafeMoveUpdatedComponent(Delta, GetActorQuat(), true, FirstHit);
	if (FirstHit.IsValidBlockingHit())
	{
		// Slide along whatever it touched for the rest of the frame, and lose the speed that went into
		// the surface: the drone skims a wall or a floor, it does not stick to it or bounce.
		FHitResult SlideHit = FirstHit;
		static_cast<UMovementComponent*>(CMC)->SlideAlongSurface(Delta, 1.0f - FirstHit.Time, FirstHit.Normal, SlideHit, true);
		CMC->Velocity = FVector::VectorPlaneProject(NewVelocity, FirstHit.Normal);
	}
	return FirstHit;
}

FVector AKamikazeDroneNPC::GetTargetFeet(const APawn* Target) const
{
	if (!Target)
	{
		return GetActorLocation();
	}
	FVector Feet = Target->GetActorLocation();
	if (const ACharacter* const TargetCharacter = Cast<ACharacter>(Target))
	{
		if (const UCapsuleComponent* const Capsule = TargetCharacter->GetCapsuleComponent())
		{
			Feet.Z -= Capsule->GetScaledCapsuleHalfHeight();
		}
	}
	return Feet;
}

FVector AKamikazeDroneNPC::ComputeHoldPoint(const APawn* Target) const
{
	const FVector Feet = GetTargetFeet(Target);

	// Bearing: the side the drone came in from, fixed when it first reached the hold (before that,
	// the side it is on now). It does not go round the player on its own: a group from one carrier
	// hangs together on the carrier's side, and nothing sneaks behind the player's head.
	float BearingDeg;
	if (bHasHoldBearing)
	{
		BearingDeg = HoldBearingDeg;
	}
	else
	{
		const FVector Away = GetActorLocation() - Feet;
		BearingDeg = (FMath::Square(Away.X) + FMath::Square(Away.Y) < 1.0f)
			? Target->GetActorRotation().Yaw + 180.0f
			: FMath::RadiansToDegrees(FMath::Atan2(Away.Y, Away.X));
	}
	BearingDeg += RepositionOffsetDeg;

	const float BearingRad = FMath::DegreesToRadians(BearingDeg);
	const FVector Out(FMath::Cos(BearingRad), FMath::Sin(BearingRad), 0.0f);
	FVector Point = Feet + Out * HoldDistance + FVector(0.0f, 0.0f, HoldHeight);

	// A loose cluster, not a pile: keep clear of the other drones.
	if (const UKamikazeStrikeSubsystem* const Queue = GetStrikeQueue())
	{
		Point += Queue->GetSeparationOffset(this, Point);
	}

	// Figure-eight drift while holding: sideways across the sector and a little up and down, never
	// still. Smooth sines, so it reads as a pilot correcting, not as noise.
	if (CurrentState == EKamikazeState::Orbiting && DriftPeriod > KINDA_SMALL_NUMBER)
	{
		const float W = UE_TWO_PI / DriftPeriod;
		const float T = GetWorld()->GetTimeSeconds() * W + DriftPhase;
		const FVector Side(-Out.Y, Out.X, 0.0f);
		Point += Side * (DriftRadius * FMath::Sin(T)) + FVector(0.0f, 0.0f, DriftVertical * FMath::Sin(2.0f * T));
	}
	return Point;
}

UKamikazeStrikeSubsystem* AKamikazeDroneNPC::GetStrikeQueue() const
{
	const UWorld* const World = GetWorld();
	return (World && HasAuthority()) ? World->GetSubsystem<UKamikazeStrikeSubsystem>() : nullptr;
}

void AKamikazeDroneNPC::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UKamikazeStrikeSubsystem* Queue = GetStrikeQueue())
	{
		Queue->Unregister(this);
	}
	Super::EndPlay(EndPlayReason);
}

FVector AKamikazeDroneNPC::ComputeStrikeAimPoint() const
{
	if (AttackPattern == EAttackPattern::Direct && BuildingTarget)
	{
		if (!DirectAttackTargetLocation.IsZero())
		{
			return DirectAttackTargetLocation;
		}
		FVector BoundsOrigin, BoundsExtent;
		BuildingTarget->GetActorBounds(true, BoundsOrigin, BoundsExtent);
		return BoundsOrigin;
	}

	const APawn* const Target = GetTargetPawn();
	if (!Target)
	{
		// No target this frame (a munition between reacquire sweeps): keep the last aim point.
		return AttackTargetPosition;
	}

	const FVector Feet = GetTargetFeet(Target);
	const FVector Aim = Feet + FVector(0.0f, 0.0f, AimHeightAboveFeet);

	// Lead by the time the strike needs to get there at its own speed. A constant velocity is caught
	// exactly; only a change the turn rate cannot follow escapes.
	const float TimeToGo = FMath::Min(FVector::Dist(GetActorLocation(), Aim) / FMath::Max(StrikeSpeed, 1.0f), MaxLeadTime);
	FVector Led = Aim + Target->GetVelocity() * TimeToGo;

	// Never below the feet: a falling target would otherwise pull the aim point into the floor.
	Led.Z = FMath::Max(Led.Z, Feet.Z + GetCapsuleComponent()->GetScaledCapsuleRadius());
	return Led;
}

FVector AKamikazeDroneNPC::ArriveVelocity(const FVector& Point, float SpeedCap, float Acceleration) const
{
	const FVector ToPoint = Point - GetActorLocation();
	const float Dist = ToPoint.Size();
	if (Dist < 1.0f)
	{
		return FVector::ZeroVector;
	}
	// The fastest speed from which it can still stop exactly on the point: v = sqrt(2 a d).
	const float Speed = FMath::Min(SpeedCap, FMath::Sqrt(2.0f * Acceleration * Dist));
	return ToPoint / Dist * Speed;
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

	const FVector Vel = CMC->Velocity;
	const float Speed = Vel.Size();
	const FVector Dir = (Speed > 1.0f) ? Vel / Speed : GetActorForwardVector();

	// Settle from the launch impulse toward the hold speed, turning gently toward where it goes next.
	const float NewSpeed = FMath::FInterpTo(Speed, HoldSpeed, DeltaTime, LaunchDecayRate);
	FVector NewDir = Dir;
	if (const APawn* const Target = GetTargetPawn())
	{
		const FVector Goal = ComputeHoldPoint(Target);
		NewDir = FMath::VInterpNormalRotationTo(Dir, (Goal - GetActorLocation()).GetSafeNormal(), DeltaTime, LaunchSteerRate);
	}
	FlyMove(NewDir * NewSpeed, DeltaTime);

	// Settled: off to the hold like any other drone. A carrier's munition too: it takes its place on
	// the ring and waits its turn in the strike queue.
	const bool bSettled = StateTimer >= 0.3f && NewSpeed <= HoldSpeed * 1.15f;
	if (bSettled || StateTimer >= MaxLaunchStabilizationTime)
	{
		SetState(EKamikazeState::Orbiting);
	}
}

void AKamikazeDroneNPC::InitiateLaunch(const FVector& LaunchVelocity)
{
	if (UCharacterMovementComponent* CMC = GetCharacterMovement())
	{
		CMC->Velocity = LaunchVelocity;
	}
	SetHandDrivenFlight(true);

	if (!LaunchVelocity.IsNearlyZero())
	{
		SetActorRotation(LaunchVelocity.Rotation());
	}

	PreviousFrameLocation = GetActorLocation();
	SetState(EKamikazeState::Launching);

	UE_LOG(LogTemp, Log, TEXT("[Kamikaze %s] InitiateLaunch | Speed=%.0f Dir=%s | Pos=(%.0f,%.0f,%.0f)"),
		*GetName(), LaunchVelocity.Size(), *LaunchVelocity.GetSafeNormal().ToCompactString(),
		GetActorLocation().X, GetActorLocation().Y, GetActorLocation().Z);
}

void AKamikazeDroneNPC::LaunchAsHomingMunition(const FVector& LaunchVelocity)
{
	bSelfDesignateTarget = true;
	bProximityTimedOut = false;
	InitiateLaunch(LaunchVelocity);
}

void AKamikazeDroneNPC::InitiateDirectAttack(AActor* Target, FVector TargetWorldLocation)
{
	if (!Target)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Kamikaze %s] InitiateDirectAttack called with null target"), *GetName());
		return;
	}

	AttackPattern = EAttackPattern::Direct;
	BuildingTarget = Target;
	DirectAttackTargetLocation = TargetWorldLocation;
	AttackTargetPosition = ComputeStrikeAimPoint();

	const FVector Dir = (AttackTargetPosition - GetActorLocation()).GetSafeNormal();
	if (UCharacterMovementComponent* CMC = GetCharacterMovement())
	{
		CMC->Velocity = Dir * StrikeSpeed;
	}
	SetHandDrivenFlight(true);
	if (!Dir.IsNearlyZero())
	{
		SetActorRotation(Dir.Rotation());
	}

	PreviousFrameLocation = GetActorLocation();
	SetState(EKamikazeState::Attacking);

	UE_LOG(LogTemp, Warning, TEXT("[Kamikaze %s] InitiateDirectAttack | Target=%s TargetPos=%s (from %s)"),
		*GetName(), *Target->GetName(), *AttackTargetPosition.ToCompactString(),
		DirectAttackTargetLocation.IsZero() ? TEXT("bounds center fallback") : TEXT("explicit hit point"));
}

void AKamikazeDroneNPC::UpdateHold(float DeltaTime, bool bCountTowardStrike)
{
	StateTimer += DeltaTime;

	UCharacterMovementComponent* CMC = GetCharacterMovement();
	if (!CMC)
	{
		return;
	}

	// Slide from one side of the sector to the other every RepositionInterval, along the arc.
	if (bCountTowardStrike && RepositionInterval > KINDA_SMALL_NUMBER)
	{
		RepositionTimer += DeltaTime;
		if (RepositionTimer >= RepositionInterval)
		{
			RepositionTimer = 0.0f;
			RepositionSide = -RepositionSide;
		}
		const float TargetOffset = RepositionSide * RepositionAngle * 0.5f;
		RepositionOffsetDeg = FMath::FInterpConstantTo(RepositionOffsetDeg, TargetOffset, DeltaTime,
			RepositionAngle / FMath::Max(RepositionTime, 0.1f));
	}

	APawn* const Target = GetTargetPawn();

	// The side it came in from becomes its side for good: set once, the first time it holds.
	if (bCountTowardStrike && !bHasHoldBearing && Target)
	{
		const FVector Away = GetActorLocation() - GetTargetFeet(Target);
		HoldBearingDeg = FMath::RadiansToDegrees(FMath::Atan2(Away.Y, Away.X));
		bHasHoldBearing = true;
	}

	// Recovery opens with a punch-out: full throttle up, keeping half the forward speed, the way a
	// pilot climbs out of a missed dive before turning back.
	if (!bCountTowardStrike && StateTimer < PunchOutTime)
	{
		const FVector Forward(CMC->Velocity.X * 0.5f, CMC->Velocity.Y * 0.5f, 0.0f);
		const FVector Desired = Forward + FVector(0.0f, 0.0f, PunchOutSpeed);
		FlyMove(StepVelocityToward(CMC->Velocity, Desired, StrikeAcceleration * DeltaTime), DeltaTime);
		return;
	}

	// Fly to the hold point and stop there. No target: brake to a hover where it is.
	FVector Desired = FVector::ZeroVector;
	bool bAtHoldPoint = false;
	if (Target)
	{
		const FVector HoldPoint = ComputeHoldPoint(Target);
		// The braking curve uses a little less than the real acceleration, so the stop never overshoots.
		Desired = ArriveVelocity(HoldPoint, HoldSpeed, HoldAcceleration * 0.8f);
		// The point drifts and slides, so "there" is a generous radius around it.
		bAtHoldPoint = FVector::DistSquared(GetActorLocation(), HoldPoint) <= FMath::Square(HoldArriveTolerance * 2.0f);
	}
	FlyMove(StepVelocityToward(CMC->Velocity, Desired, HoldAcceleration * DeltaTime), DeltaTime);

	if (!Target)
	{
		return;
	}

	// Recovery: back at the point, or out of time trying, means the miss is over.
	if (!bCountTowardStrike)
	{
		if (bAtHoldPoint || StateTimer >= RecoveryMaxTime)
		{
			SetState(EKamikazeState::Orbiting);
		}
		return;
	}

	// Shot while holding: the rest of the wait is skipped and the drone goes to the front of the
	// queue. The schedule still spaces it from the strike before.
	if (bIsRetaliating)
	{
		HoldTimer = FMath::Max(HoldTimer, HoldTimeBeforeStrike);
	}

	// The wait counts only at the point and with a clear line to the target, so a strike always
	// comes from where the player could see it hanging.
	if (bAtHoldPoint && HoldTimer < HoldTimeBeforeStrike)
	{
		FHitResult LOSHit;
		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(KamikazeHoldLOS), false, this);
		QueryParams.AddIgnoredActor(Target);
		const FVector AimPoint = GetTargetFeet(Target) + FVector(0.0f, 0.0f, AimHeightAboveFeet);
		if (!GetWorld()->LineTraceSingleByChannel(LOSHit, GetActorLocation(), AimPoint, ECC_Visibility, QueryParams))
		{
			HoldTimer += DeltaTime;
		}
	}

	if (HoldTimer >= HoldTimeBeforeStrike)
	{
		bProximityTimedOut = true;

		// The schedule decides when: it knows when the previous strike at this player lands, and
		// lets this one go only if it will land late enough for the player to deal with both. So it
		// needs to know how long this strike takes: wind-up, swinging the nose round, getting up to
		// speed, and the run itself. A target with no schedule behind it (a faction pawn) is struck
		// as soon as the wait is over.
		UKamikazeStrikeSubsystem* const Queue = GetStrikeQueue();
		const bool bQueued = Queue && QueueTarget.Get() == Target;
		if (!bStrikeGranted)
		{
			// An arcing run is longer than the straight line: the curve holds a constant angle off
			// the line of sight, which stretches the path by 1 / cos of that angle.
			const float ArcStretch = 1.0f / FMath::Cos(FMath::Atan(0.5f * StrikeArc));
			const float RunDistance = FVector::Dist(GetActorLocation(), ComputeStrikeAimPoint()) * ArcStretch;
			const float FlightTime = WindUpTime + StrikeTurnOutWindow * 0.5f
				+ 0.5f * StrikeSpeed / FMath::Max(StrikeAcceleration, 1.0f)
				+ RunDistance / FMath::Max(StrikeSpeed, 1.0f);
			bStrikeGranted = bQueued ? Queue->RequestStrike(this, Target, bIsRetaliating, FlightTime) : true;
		}
		if (bStrikeGranted && bSelfStrike)
		{
			BeginAttack(bIsRetaliating);
		}
	}
}

void AKamikazeDroneNPC::UpdateStrike(float DeltaTime)
{
	StateTimer += DeltaTime;

	UCharacterMovementComponent* CMC = GetCharacterMovement();
	if (!CMC)
	{
		return;
	}

	const bool bDirect = (AttackPattern == EAttackPattern::Direct && BuildingTarget);

	// Live aim every frame, never switched off before impact.
	AttackTargetPosition = ComputeStrikeAimPoint();

	FVector ToAim = AttackTargetPosition - GetActorLocation();

	// Arc: steer at a point pushed sideways by a share of the remaining distance. The push shrinks
	// with the distance, so the path is a curve that closes onto the target instead of missing it;
	// the drone comes in from the side rather than down the line of sight. Straight when 0.
	if (!bDirect && StrikeArc > KINDA_SMALL_NUMBER)
	{
		const FVector Side = FVector::CrossProduct(ToAim, FVector::UpVector).GetSafeNormal();
		ToAim += Side * (StrikeArcSide * StrikeArc * 0.5f * ToAim.Size());
	}
	const FVector DesiredDir = ToAim.GetSafeNormal();

	// Wind-up: back off and up for a moment, nose dipping (the tilt reads the braking and the push),
	// then the run. The pilot lining up the dive, and the player's cue that this one goes now.
	const bool bWindingUp = !bDirect && StateTimer < WindUpTime;
	if (bWindingUp)
	{
		const FVector Back(-DesiredDir.X, -DesiredDir.Y, 0.0f);
		const FVector WindUpDir = (Back.GetSafeNormal() * 0.6f + FVector(0.0f, 0.0f, 0.8f)).GetSafeNormal();
		const float WindUpSpeed = 1.5f * WindUpDistance / FMath::Max(WindUpTime, 0.05f);
		FlyMove(StepVelocityToward(CMC->Velocity, WindUpDir * WindUpSpeed, StrikeAcceleration * DeltaTime), DeltaTime);
		CheckContact();
		return;
	}

	const float Speed = CMC->Velocity.Size();
	const FVector CurrentDir = (Speed > 1.0f) ? CMC->Velocity / Speed : DesiredDir;

	// Constant top speed and a turn limited by sideways grip, like a real quad: StrikeTurnRate at full
	// speed, faster when slow (so it can snap its nose round out of the wind-up), never above 720.
	// The path is a readable curve, which is what makes it possible to track with the crosshair.
	const float TurnRate = FMath::Min(720.0f, StrikeTurnRate * StrikeSpeed / FMath::Max(Speed, 100.0f));
	const FVector NewDir = FMath::VInterpNormalRotationTo(CurrentDir, DesiredDir, DeltaTime, TurnRate);

	// Nose first, then throttle. Coming out of the wind-up the drone points away from the target;
	// until the nose is round, the speed is held low, which is also what keeps the turn fast. Only in
	// that opening moment: later in the run a drone that has lost the target overshoots and misses,
	// it does not brake to follow.
	const bool bTurningOut = !bDirect && StateTimer < WindUpTime + StrikeTurnOutWindow;
	const bool bNoseRound = FVector::DotProduct(NewDir, DesiredDir) >= 0.7f;
	const float SpeedCap = (bTurningOut && !bNoseRound) ? FMath::Min(StrikeSpeed, 300.0f) : StrikeSpeed;
	const float NewSpeed = FMath::FInterpConstantTo(Speed, SpeedCap, DeltaTime, StrikeAcceleration);
	const FHitResult Hit = FlyMove(NewDir * NewSpeed, DeltaTime);

	if (CheckContact())
	{
		return;
	}

	if (bDirect)
	{
		// A turret's drone flies into its building: whatever it touches ends the run, and the building
		// is told directly, because DoExplosion only damages pawns. A player building takes the
		// tuned number; the old marked skyscraper (ATurretBuilding) is meant to go in one hit.
		if (Hit.IsValidBlockingHit())
		{
			if (Hit.GetActor() == BuildingTarget)
			{
				FDamageEvent DmgEvent;
				DmgEvent.DamageTypeClass = UDamageType::StaticClass();
				const float Damage = BuildingTarget->IsA<ABuildableActor>() ? BuildingStrikeDamage : 99999.0f;
				BuildingTarget->TakeDamage(Damage, DmgEvent, GetController(), this);
			}
			CurrentState = EKamikazeState::Dead;
			TriggerCollisionExplosion();
			KamikazeDie();
		}
		return;
	}

	if (CVarKamikazeDebug.GetValueOnGameThread() >= 2)
	{
		DrawDebugSphere(GetWorld(), AttackTargetPosition, 25.0f, 8, FColor::Red, false, 0.0f);
	}

	// Miss: the aim point is behind the drone (it flew past, or the target moved where the turn rate
	// could not follow), or the run took too long. Punch out and come back to the hold. Judged only
	// once the drone is actually running, not while it is still turning out of the wind-up.
	const bool bRunning = !bTurningOut && NewSpeed >= StrikeSpeed * 0.8f;
	const bool bAimBehind = bRunning && FVector::DotProduct(AttackTargetPosition - GetActorLocation(), NewDir) < 0.0f;
	if (bAimBehind || StateTimer >= StrikeMaxTime + WindUpTime)
	{
		SetState(EKamikazeState::Recovery);
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

	// Stop CMC entirely — we drive position directly via swept MoveUpdatedComponent
	if (UCharacterMovementComponent* CMC = GetCharacterMovement())
	{
		CMC->StopMovementImmediately();
		CMC->DisableMovement();  // MOVE_None — CMC won't interfere
	}

	// Collision policy is uniform: world always blocks, pawns detected by sweeps/overlaps.
	// No per-state response mutations — the parried drone flies with the same rules.

	// Get player look direction
	FVector PlayerLoc = FVector::ZeroVector;
	FVector PlayerForward = FVector::ForwardVector;
	APawn* AttackerPawn = nullptr;
	if (AttackerController)
	{
		AttackerPawn = AttackerController->GetPawn();
		if (AttackerPawn)
		{
			PlayerLoc = AttackerPawn->GetActorLocation();
			PlayerForward = AttackerPawn->GetControlRotation().Vector();
		}
	}

	// Find redirect target in cone
	ParryTarget = FindParryTarget(AttackerPawn, PlayerLoc, PlayerForward);

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

	// Reset PreviousFrameLocation so the sweep in first UpdateParried frame
	// doesn't trace from some old position and instantly hit geometry
	PreviousFrameLocation = GetActorLocation();

	SetState(EKamikazeState::Parried);
}

APawn* AKamikazeDroneNPC::FindParryTarget(const APawn* AttackerPawn, const FVector& PlayerLocation, const FVector& PlayerForward) const
{
	const float CosHalfAngle = FMath::Cos(FMath::DegreesToRadians(ParryConeLookHalfAngle));
	APawn* BestTarget = nullptr;
	float BestDistSq = ParryConeLookDistance * ParryConeLookDistance;

	// Overlap sphere to find candidates
	TArray<FOverlapResult> Overlaps;
	FCollisionShape Sphere = FCollisionShape::MakeSphere(ParryConeLookDistance);
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	GetWorld()->OverlapMultiByChannel(Overlaps, PlayerLocation, FQuat::Identity, ECC_Pawn, Sphere, QueryParams);

	for (const FOverlapResult& Overlap : Overlaps)
	{
		APawn* Candidate = Cast<APawn>(Overlap.GetActor());
		if (!Candidate || Candidate == this)
		{
			continue;
		}
		if (AShooterNPC* CandidateNPC = Cast<AShooterNPC>(Candidate))
		{
			if (CandidateNPC->IsDead())
			{
				continue;
			}
		}

		// The parried drone is a weapon in the parrier's hands: it redirects into the
		// parrier's enemies. Same-side pawns (including other players) are not candidates.
		const AActor* HostilityRef = AttackerPawn ? static_cast<const AActor*>(AttackerPawn) : static_cast<const AActor*>(this);
		if (!PolarityTeams::AreHostile(HostilityRef, Candidate))
		{
			continue;
		}

		// Cone check from parrier position
		const FVector ToTarget = Candidate->GetActorLocation() - PlayerLocation;
		const float DistSq = ToTarget.SizeSquared();
		const FVector DirToTarget = ToTarget.GetSafeNormal();
		const float CosAngle = FVector::DotProduct(PlayerForward, DirToTarget);

		if (CosAngle >= CosHalfAngle && DistSq < BestDistSq)
		{
			// LOS check
			FHitResult LOSHit;
			FCollisionQueryParams LOSParams;
			LOSParams.AddIgnoredActor(this);
			LOSParams.AddIgnoredActor(Candidate);
			const bool bBlocked = GetWorld()->LineTraceSingleByChannel(
				LOSHit, PlayerLocation, Candidate->GetActorLocation(), ECC_Visibility, LOSParams);
			if (!bBlocked)
			{
				BestTarget = Candidate;
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
		APawn* TargetPawn = Cast<APawn>(ParryTarget.Get());
		const bool bTargetAlive = TargetPawn && !(Cast<AShooterNPC>(TargetPawn) && Cast<AShooterNPC>(TargetPawn)->IsDead());
		if (bTargetAlive)
		{
			const FVector ToTarget = TargetPawn->GetActorLocation() - GetActorLocation();
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

	// --- Swept move: one mover for every state, world always blocks ---
	const FVector OldLocation = GetActorLocation();
	const FVector MoveDelta = (ParryDirection * ParrySpiralForwardSpeed + GravityComponent / FMath::Max(DeltaTime, KINDA_SMALL_NUMBER)) * DeltaTime + SpiralOffset;
	const FRotator KeepRot = GetActorRotation();

	FHitResult MoveHit;
	if (UCharacterMovementComponent* CMC = GetCharacterMovement())
	{
		CMC->MoveUpdatedComponent(MoveDelta, KeepRot, true, &MoveHit);
		CMC->Velocity = MoveDelta / FMath::Max(DeltaTime, KINDA_SMALL_NUMBER);
	}
	else
	{
		SetActorLocation(GetActorLocation() + MoveDelta, true);
	}

	// Blocked by geometry → explode at the impact point
	if (MoveHit.bBlockingHit)
	{
		DoExplosion(ParryExplosionRadius, ParryExplosionDamage, UDamageType_KamikazeExplosion::StaticClass(), true);
		KamikazeDie();
		return;
	}

	// --- Hostile pawn contact at the new position ---
	{
		TArray<FOverlapResult> Overlaps;
		FCollisionShape Sphere = FCollisionShape::MakeSphere(CollisionRadius + 20.0f);
		FCollisionQueryParams QueryParams;
		QueryParams.AddIgnoredActor(this);

		GetWorld()->OverlapMultiByChannel(Overlaps, GetActorLocation(), FQuat::Identity, ECC_Pawn, Sphere, QueryParams);
		for (const FOverlapResult& Overlap : Overlaps)
		{
			APawn* OverlapPawn = Cast<APawn>(Overlap.GetActor());
			if (OverlapPawn && PolarityTeams::AreHostile(this, OverlapPawn))
			{
				DoExplosion(ParryExplosionRadius, ParryExplosionDamage, UDamageType_KamikazeExplosion::StaticClass(), true);
				KamikazeDie();
				return;
			}
		}
	}

	// Spin the mesh for visual tumble effect
	if (DroneMesh)
	{
		const float SpinDelta = ParryMeshSpinSpeed * DeltaTime;
		DroneMesh->AddLocalRotation(FRotator(SpinDelta * 0.7f, SpinDelta, SpinDelta * 0.3f));
	}
}

void AKamikazeDroneNPC::BeginAttack(bool bRetaliation)
{
	// The StateTree calls this, and so does the drone itself when bSelfStrike is on; both go through
	// the same gate, so a strike starts once. A strike is only ever launched from the hold after a
	// full wait, or straight away as retaliation, or out of a munition's launch: the player always
	// gets either the visible hang or the shot that provoked it.
	// Only from the hold, after the full wait (or cut short by being shot), and only once the strike
	// queue has said yes: whoever calls this, the player gets one strike at a time.
	const bool bFromHold = CurrentState == EKamikazeState::Orbiting && bProximityTimedOut && bStrikeGranted;
	if (!HasAuthority() || !bFromHold)
	{
		return;
	}

	bIsRetaliating = bIsRetaliating || bRetaliation;
	bProximityTimedOut = false;
	bStrikeGranted = false;
	HoldTimer = 0.0f;
	StrikeArcSide = FMath::RandBool() ? 1.0f : -1.0f;
	AttackTargetPosition = ComputeStrikeAimPoint();

	SetHandDrivenFlight(true);
	PreviousFrameLocation = GetActorLocation();
	SetState(EKamikazeState::Attacking);

	// Commit whine — the audible telegraph of the strike
	if (TelegraphSound)
	{
		UGameplayStatics::PlaySoundAtLocation(GetWorld(), TelegraphSound, GetActorLocation());
	}
}

bool AKamikazeDroneNPC::CheckContact()
{
	// Contact, not proximity: the drone's own sphere, inflated a little, touching a hostile pawn along
	// this frame's path. Swept from last frame so nothing is skipped at speed.
	const FVector SweepStart = PreviousFrameLocation;
	const FVector SweepEnd = GetActorLocation();
	const float Radius = GetCapsuleComponent()->GetScaledCapsuleRadius() + ContactFuseRadius;

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(KamikazeContact), false, this);
	TArray<FHitResult> Hits;
	GetWorld()->SweepMultiByChannel(Hits, SweepStart, SweepEnd, FQuat::Identity, ECC_Pawn,
		FCollisionShape::MakeSphere(Radius), QueryParams);

	for (const FHitResult& Hit : Hits)
	{
		APawn* const HitPawn = Cast<APawn>(Hit.GetActor());
		if (HitPawn && PolarityTeams::AreHostile(this, HitPawn))
		{
			CurrentState = EKamikazeState::Dead;
			TriggerCollisionExplosion();
			KamikazeDie();
			return true;
		}
	}
	return false;
}

// ==================== Damage ====================

UMeshComponent* AKamikazeDroneNPC::GetHitFlashMeshComponent() const
{
	return DroneMesh;
}

float AKamikazeDroneNPC::TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (bIsDead || bIsParried)
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

		// Same side only, as in AShooterNPC::TakeDamage: a shooter from another faction hurts this
		// drone exactly like a player does.
		if (!bIsCollisionDamage)
		{
			AActor* DamageOwner = DamageCauser->GetOwner();
			if (AShooterNPC* Shooter = Cast<AShooterNPC>(DamageCauser) ? Cast<AShooterNPC>(DamageCauser) : Cast<AShooterNPC>(DamageOwner))
			{
				if (!PolarityTeams::AreHostile(this, Shooter))
				{
					return 0.0f;
				}
			}
		}

		if (EventInstigator)
		{
			if (AShooterNPC* InstigatorNPC = Cast<AShooterNPC>(EventInstigator->GetPawn()))
			{
				if (!PolarityTeams::AreHostile(this, InstigatorNPC))
				{
					return 0.0f;
				}
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
			// Skipped for a blade that states its own ionization: it has already charged this target
			// through the ordinary weapon path, and paying out here as well would land the same hit
			// twice, leaving the number on the weapon describing half of what actually happens.
			// Everything else still comes through here -- bare fists, an enemy hitting a player, a
			// drone -- because for those this IS where the amount is authored.
			APawn* Attacker = EventInstigator->GetPawn();
			if (Attacker && !AShooterWeapon_Melee::AttackerOverridesLegacyMeleeCharge(Attacker))
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

	// Retaliation: flag only — the StateTree condition reads it and calls BeginAttack.
	// The pawn never starts an attack on its own (single attack authority).
	if (bRetaliateOnDamage && CurrentState == EKamikazeState::Orbiting)
	{
		bIsRetaliating = true;
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

	// Unregister from coordinator, and from the strike queue (ends its strike, frees its ring slot)
	UnregisterFromCoordinator();
	if (UKamikazeStrikeSubsystem* Queue = GetStrikeQueue())
	{
		Queue->Unregister(this);
	}

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

	case EKamikazeState::Attacking:
	{
		// Killed during attack — check distance to target
		if (APawn* Target = GetTargetPawn())
		{
			const float DistToTarget = FVector::Dist(GetActorLocation(), Target->GetActorLocation());
			if (DistToTarget <= AttackDeathDistanceThreshold)
			{
				// Close to target — air explosion
				TriggerAirExplosion();
				break;
			}
		}
		// Far from target — just debris
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
		UE_LOG(LogTemp, Warning, TEXT("[Kamikaze %s] >>> TriggerDebrisFall (crash VFX/SFX, YES HP)"), *GetName());
	}

	// Crash VFX
	if (CrashExplosionFX && !KamikazeNoVFX())
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), CrashExplosionFX, GetActorLocation());
	}

	// Crash SFX
	if (CrashSound)
	{
		UGameplayStatics::PlaySoundAtLocation(GetWorld(), CrashSound, GetActorLocation());
	}

	// Enable gravity so debris/mesh falls
	if (UCharacterMovementComponent* CMC = GetCharacterMovement())
	{
		CMC->GravityScale = 1.0f;
		CMC->SetMovementMode(MOVE_Falling);
	}

	// Loot (reward for killing). What drops is the blueprint's LootDrop list.
	if (!bSuppressDeathDrops && LootDrop)
	{
		LootDrop->DropLoot(MakeLootContext(0.0f));
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
	if (FXToUse && !KamikazeNoVFX())
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

		// Use a player pawn as DamageCauser to bypass NPC friendly-fire and show damage numbers.
		// The drone's own target is the closest thing to "who is responsible" that exists today.
		// TODO(COOP): a parried drone should credit whoever parried it, which needs the parry
		// instigator stored on the drone. Attribution pass, not this one.
		APawn* PlayerPawn = GetTargetPawn();

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

	// Loot, on the same explosions that used to drop health.
	if (!bSuppressDeathDrops && bDropHealthPickup && LootDrop)
	{
		LootDrop->DropLoot(MakeLootContext(0.0f));
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

void AKamikazeDroneNPC::ApplyKnockback(const FVector& InKnockbackDirection, float Distance, float Duration, const FVector& AttackerLocation, bool bKeepEMFEnabled, EKnockbackStyle Style)
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

	// Ignore collision with whoever did the knocking, so the drone does not jitter against them.
	// The attacker is identified by position — ask the team system who was standing there.
	if (!AttackerLocation.IsZero())
	{
		APawn* Nearest = PolarityTeams::FindNearestHostilePawn(this);
		if (Nearest && FVector::Dist(Nearest->GetActorLocation(), AttackerLocation) < 300.0f)
		{
			KnockbackIgnoreActor = Nearest;
			MoveIgnoreActorAdd(Nearest);
		}
	}

	Super::ApplyKnockback(InKnockbackDirection, Distance, Duration, AttackerLocation, bKeepEMFEnabled, Style);
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

	// Restore flying mode, and take the flight back from whatever drove the knockback.
	if (HasAuthority() && !bIsDead && CurrentState != EKamikazeState::Parried)
	{
		SetHandDrivenFlight(true);
	}
	else if (UCharacterMovementComponent* CMC = GetCharacterMovement())
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
	return CurrentState == EKamikazeState::Attacking
		|| CurrentState == EKamikazeState::PostAttack
		|| CurrentState == EKamikazeState::Recovery
		|| CurrentState == EKamikazeState::Parried;
}

// ==================== Helpers ====================

APawn* AKamikazeDroneNPC::GetTargetPawn() const
{
	// The strike queue shares the players out evenly; when it has handed this drone one, that is the
	// answer. Everything below is for a drone without a player to fly at (faction fights, no players).
	if (APawn* const Queued = QueueTarget.Get())
	{
		if (!IsTargetGone(Queued))
		{
			return Queued;
		}
	}

	// A self-designating munition answers before the coordinator is asked: it is not registered
	// with the coordinator, and the whole point of the mode is that each drone picks for itself
	// and switches to somebody else once its own target is gone.
	if (bSelfDesignateTarget)
	{
		if (APawn* const Current = CachedTarget.Get())
		{
			if (!IsTargetGone(Current))
			{
				return Current;
			}
		}

		const UWorld* const World = GetWorld();
		const float Now = World ? World->GetTimeSeconds() : 0.0f;
		if (Now - LastReacquireTime < TargetReacquireInterval)
		{
			// Between sweeps: no target rather than a stale corpse. Callers keep the last aim point.
			return nullptr;
		}
		LastReacquireTime = Now;

		APawn* Fresh = nullptr;
		if (const AAICombatCoordinator* const Coordinator = AAICombatCoordinator::GetCoordinator(this))
		{
			// The coordinator's own sweep is the cheap one (players plus registered NPCs).
			Fresh = Coordinator->FindNearestHostile(const_cast<AKamikazeDroneNPC*>(this));
		}
		if (!Fresh || IsTargetGone(Fresh))
		{
			Fresh = PolarityTeams::FindNearestHostilePawn(this);
		}
		CachedTarget = (Fresh && !IsTargetGone(Fresh)) ? Fresh : nullptr;
		return CachedTarget.Get();
	}

	// Ask the one system that has an opinion first. The coordinator already decides who each NPC is
	// fighting, with the switch margin and the delay that stop a drone flipping between two
	// candidates mid-orbit, and since the target work it is the only place a decoy or a faction
	// enemy can enter the answer. A drone reading "nearest player" instead was a second opinion that
	// could disagree with the formation being laid out around it.
	if (const AAICombatCoordinator* const Coordinator = AAICombatCoordinator::GetCoordinator(this))
	{
		// Cast because the coordinator also hands out decoy props, and a drone dives at pawns.
		if (APawn* const Assigned = Cast<APawn>(Coordinator->GetTargetFor(const_cast<AKamikazeDroneNPC*>(this))))
		{
			CachedTarget = Assigned;
			return Assigned;
		}
	}

	if (APawn* Cached = CachedTarget.Get())
	{
		return Cached;
	}

	// No coordinator in the level, or this drone is not registered with it: nearest hostile, which
	// is the same rule as before except that it now includes the other faction.
	APawn* Nearest = PolarityTeams::FindNearestHostilePawn(this);
	CachedTarget = Nearest;
	return Nearest;
}
