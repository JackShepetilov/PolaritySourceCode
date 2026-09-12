// KamikazeCarrierDrone.cpp

#include "KamikazeCarrierDrone.h"
#include "KamikazeDroneNPC.h"
#include "KamikazeStrikeSubsystem.h"
#include "FlyingAIMovementComponent.h"
#include "AICombatCoordinator.h"
#include "AIController.h"
#include "AI/PolarityTeams.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StateTreeAIComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "ShooterCharacter.h"
#include "TimerManager.h"

namespace
{
	/** Seconds between re-picking the target, and between fresh move orders to the standoff point. */
	constexpr float CarrierReacquireInterval = 1.0f;
	constexpr float CarrierMoveOrderInterval = 0.25f;

	bool IsGoneTarget(const APawn* Pawn)
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

	FVector FeetOf(const APawn* Pawn)
	{
		FVector Feet = Pawn->GetActorLocation();
		if (const ACharacter* const Character = Cast<ACharacter>(Pawn))
		{
			if (const UCapsuleComponent* const Capsule = Character->GetCapsuleComponent())
			{
				Feet.Z -= Capsule->GetScaledCapsuleHalfHeight();
			}
		}
		return Feet;
	}
}

AKamikazeCarrierDrone::AKamikazeCarrierDrone(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = true;

	// A pawn-typed box that weapon traces find (they query by object type Pawn) and that responds to
	// nothing, so it never blocks movement, walls or the player. Attached to the mesh so it tilts
	// with the airframe.
	Hitbox = CreateDefaultSubobject<UBoxComponent>(TEXT("Hitbox"));
	USceneComponent* const HitboxParent = DroneMesh ? static_cast<USceneComponent*>(DroneMesh.Get()) : GetRootComponent();
	Hitbox->SetupAttachment(HitboxParent);
	Hitbox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Hitbox->SetCollisionObjectType(ECC_Pawn);
	Hitbox->SetCollisionResponseToAllChannels(ECR_Ignore);
	Hitbox->SetGenerateOverlapEvents(false);
	Hitbox->SetCanEverAffectNavigation(false);
	Hitbox->CanCharacterStepUpOn = ECB_No;
	Hitbox->InitBoxExtent(FVector(150.0f, 150.0f, 50.0f));
}

void AKamikazeCarrierDrone::FitHitboxToMesh()
{
	if (!Hitbox || !DroneMesh || !DroneMesh->GetStaticMesh())
	{
		return;
	}
	// The box lives in the mesh's local space (it is attached to it), so the mesh's own bounds fit it
	// directly and the mesh scale applies to both. The padding is asked for in world units, so it is
	// divided back by that scale.
	const FBox LocalBox = DroneMesh->GetStaticMesh()->GetBoundingBox();
	const FVector Scale = DroneMesh->GetComponentScale().GetAbs();
	const FVector Pad(
		HitboxPadding / FMath::Max(Scale.X, KINDA_SMALL_NUMBER),
		HitboxPadding / FMath::Max(Scale.Y, KINDA_SMALL_NUMBER),
		HitboxPadding / FMath::Max(Scale.Z, KINDA_SMALL_NUMBER));
	Hitbox->SetRelativeLocation(LocalBox.GetCenter());
	Hitbox->SetBoxExtent(LocalBox.GetExtent() + Pad);
}

void AKamikazeCarrierDrone::BeginPlay()
{
	Super::BeginPlay();

	PayloadRemaining = PayloadCapacity;

	if (bFitHitboxToMesh)
	{
		FitHitboxToMesh();
	}

	// A self-driven carrier never shoots, whatever the Blueprint it was copied from says: its weapon
	// is the swarm. The drone's own auto-engage would otherwise fire at anything in range.
	if (bSelfDriven)
	{
		bAutoEngage = false;
	}
}

void AKamikazeCarrierDrone::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Authority only: a munition is a spawned pawn, and a client spawning its own would give every
	// machine a different swarm. The flight replicates from the server like any drone's.
	if (!HasAuthority() || IsDead())
	{
		return;
	}

	if (bSelfDriven)
	{
		TickSelfDriven(DeltaTime);
		return;
	}

	// The automatic trigger is the fallback for a carrier running the stock drone StateTree, which
	// has no deploy state.
	if (!bAutoDeploy || !CanDeploySalvo())
	{
		return;
	}

	const UWorld* const World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.0f;
	if (Now - LastAutoCheckTime < AutoDeployCheckInterval)
	{
		return;
	}
	LastAutoCheckTime = Now;

	// Cheap answer first: the coordinator keeps players and registered NPCs in two arrays.
	APawn* Hostile = nullptr;
	if (const AAICombatCoordinator* const Coordinator = AAICombatCoordinator::GetCoordinator(this))
	{
		Hostile = Coordinator->FindNearestHostile(this);
	}
	if (!Hostile)
	{
		Hostile = PolarityTeams::FindNearestHostilePawn(this);
	}

	if (Hostile && FVector::Dist(GetActorLocation(), Hostile->GetActorLocation()) <= AutoDeployRange)
	{
		DeploySalvo();
	}
}

void AKamikazeCarrierDrone::StopDroneStateTree()
{
	if (bStateTreeStopped)
	{
		return;
	}
	// The controller possesses the pawn after BeginPlay, and its tree starts then, so this is asked
	// every frame until there is a tree to stop. Stopped for good: fire positions, shooting and
	// evasive dashes are what a gunship drone does, and the carrier is not one.
	if (const AAIController* const AIController = Cast<AAIController>(GetController()))
	{
		if (UStateTreeAIComponent* const StateTree = AIController->FindComponentByClass<UStateTreeAIComponent>())
		{
			StateTree->StopLogic(TEXT("Carrier drives itself"));
			StopShooting();
			bStateTreeStopped = true;
		}
	}
}

void AKamikazeCarrierDrone::TickSelfDriven(float DeltaTime)
{
	StopDroneStateTree();

	// Target: the nearest live hostile, re-picked once a second. A new target means a new side.
	TargetReacquireTimer -= DeltaTime;
	APawn* Target = StandoffTarget.Get();
	if (IsGoneTarget(Target) || TargetReacquireTimer <= 0.0f)
	{
		TargetReacquireTimer = CarrierReacquireInterval;
		APawn* Fresh = nullptr;
		if (const AAICombatCoordinator* const Coordinator = AAICombatCoordinator::GetCoordinator(this))
		{
			Fresh = Coordinator->FindNearestHostile(this);
		}
		if (IsGoneTarget(Fresh))
		{
			Fresh = PolarityTeams::FindNearestHostilePawn(this);
		}
		if (IsGoneTarget(Fresh))
		{
			Fresh = nullptr;
		}
		if (Fresh != Target)
		{
			StandoffTarget = Fresh;
			bHasStandoffBearing = false;
			Target = Fresh;
		}
	}

	UFlyingAIMovementComponent* const Mover = GetFlyingMovement();
	if (!Target || !Mover)
	{
		return;
	}

	// Standoff point: on the side it came in from, swinging across its sector now and then so it has
	// to be found again, never closer than StandoffDistance.
	const FVector Feet = FeetOf(Target);
	if (!bHasStandoffBearing)
	{
		const FVector Away = GetActorLocation() - Feet;
		StandoffBearingDeg = FMath::RadiansToDegrees(FMath::Atan2(Away.Y, Away.X));
		bHasStandoffBearing = true;
	}
	// Hang still while dropping and for a moment after: the drop is when the player is looking at it,
	// and that is when it should be easiest to hit. The swing (and its clock) waits.
	const float Now = GetWorld()->GetTimeSeconds();
	const bool bHoldingStill = IsSalvoInProgress() || Now - LastSalvoTime < HoldStillAfterDrop;
	if (RepositionInterval > KINDA_SMALL_NUMBER && !bHoldingStill)
	{
		RepositionTimer += DeltaTime;
		if (RepositionTimer >= RepositionInterval)
		{
			RepositionTimer = 0.0f;
			RepositionSide = -RepositionSide;
		}
		RepositionOffsetDeg = FMath::FInterpConstantTo(RepositionOffsetDeg, RepositionSide * RepositionAngle * 0.5f,
			DeltaTime, RepositionAngle / FMath::Max(RepositionTime, 0.1f));
	}
	const float BearingRad = FMath::DegreesToRadians(StandoffBearingDeg + RepositionOffsetDeg);
	const FVector StandoffPoint = Feet
		+ FVector(FMath::Cos(BearingRad), FMath::Sin(BearingRad), 0.0f) * StandoffDistance
		+ FVector(0.0f, 0.0f, StandoffHeight);

	MoveOrderTimer -= DeltaTime;
	if (MoveOrderTimer <= 0.0f)
	{
		MoveOrderTimer = CarrierMoveOrderInterval;
		// Unclamped: the standoff height is the carrier's own decision, not the drone hover band's.
		Mover->FlyToLocationUnclamped(StandoffPoint, StandoffTolerance * 0.5f);
	}

	// Drop only from position, with the target in sight, and only while the target is not already
	// busy with enough drones: the carrier feeds the fight, it does not bury the player.
	const bool bInPosition = FVector::DistSquared(GetActorLocation(), StandoffPoint) <= FMath::Square(StandoffTolerance);
	if (!bInPosition || !CanDeploySalvo())
	{
		return;
	}

	int32 DronesOnTarget = 0;
	if (const UKamikazeStrikeSubsystem* const Queue = GetWorld()->GetSubsystem<UKamikazeStrikeSubsystem>())
	{
		DronesOnTarget = Queue->GetDronesOn(Target);
	}
	if (DronesOnTarget + DronesPerSalvo > MaxDronesPerTarget)
	{
		return;
	}

	FHitResult LOSHit;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(CarrierLOS), false, this);
	QueryParams.AddIgnoredActor(Target);
	const FVector Seen = Feet + FVector(0.0f, 0.0f, 100.0f);
	if (GetWorld()->LineTraceSingleByChannel(LOSHit, GetActorLocation(), Seen, ECC_Visibility, QueryParams))
	{
		return;
	}

	DeploySalvo();
}

bool AKamikazeCarrierDrone::CanDeploySalvo() const
{
	if (!HasAuthority() || IsDead() || (!bInfinitePayload && PayloadRemaining <= 0) || PendingInSalvo > 0 || !PayloadDroneClass)
	{
		return false;
	}
	return GetSalvoCooldownRemaining() <= 0.0f;
}

float AKamikazeCarrierDrone::GetSalvoCooldownRemaining() const
{
	const UWorld* const World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.0f;
	return FMath::Max(0.0f, (LastSalvoTime + SalvoCooldown) - Now);
}

bool AKamikazeCarrierDrone::DeploySalvo()
{
	if (!CanDeploySalvo())
	{
		return false;
	}

	const UWorld* const World = GetWorld();
	LastSalvoTime = World ? World->GetTimeSeconds() : 0.0f;

	// The first munition leaves now; the rest are owed and drop on the interval timer.
	PendingInSalvo = (bInfinitePayload ? DronesPerSalvo : FMath::Min(DronesPerSalvo, PayloadRemaining)) - 1;

	if (!DeployKamikaze())
	{
		PendingInSalvo = 0;
		return false;
	}

	if (PendingInSalvo > 0)
	{
		GetWorldTimerManager().SetTimer(SalvoTimerHandle, this,
			&AKamikazeCarrierDrone::DeployNextInSalvo, DeployInterval, true);
	}

	return true;
}

void AKamikazeCarrierDrone::DeployNextInSalvo()
{
	// Dying mid-salvo stops it: the bay went with the hull.
	if (IsDead() || (!bInfinitePayload && PayloadRemaining <= 0) || !DeployKamikaze())
	{
		PendingInSalvo = 0;
	}
	else
	{
		--PendingInSalvo;
	}

	if (PendingInSalvo <= 0)
	{
		GetWorldTimerManager().ClearTimer(SalvoTimerHandle);
	}
}

FTransform AKamikazeCarrierDrone::GetNextHardpointTransform()
{
	const FRotator CarrierRotation = GetActorRotation();

	// Named sockets first, cycled so a salvo does not fall out of the same hole.
	if (HardpointSockets.Num() > 0 && DroneMesh)
	{
		const int32 Index = NextHardpointIndex % HardpointSockets.Num();
		++NextHardpointIndex;

		const FName SocketName = HardpointSockets[Index];
		if (DroneMesh->DoesSocketExist(SocketName))
		{
			return FTransform(CarrierRotation, DroneMesh->GetSocketLocation(SocketName));
		}
	}

	// Fallback ring under the hull. Deterministic per index rather than random, so consecutive
	// drops separate instead of occasionally stacking on the same spot.
	const int32 RingSlots = FMath::Max(DronesPerSalvo, 1);
	const int32 Slot = NextHardpointIndex % RingSlots;
	++NextHardpointIndex;

	const float Angle = (2.0f * PI * Slot) / RingSlots;
	const FVector LocalOffset(
		FMath::Cos(Angle) * HardpointRingRadius,
		FMath::Sin(Angle) * HardpointRingRadius,
		HardpointDropOffset);

	return FTransform(CarrierRotation, GetActorLocation() + CarrierRotation.RotateVector(LocalOffset));
}

bool AKamikazeCarrierDrone::DeployKamikaze()
{
	UWorld* const World = GetWorld();
	if (!World || !HasAuthority() || (!bInfinitePayload && PayloadRemaining <= 0) || !PayloadDroneClass)
	{
		return false;
	}

	const FTransform SpawnTransform = GetNextHardpointTransform();

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.Instigator = this;
	// The bay is inside the hull, so the spawn point always overlaps the carrier. The munition
	// leaves it within a frame under its own launch velocity.
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AKamikazeDroneNPC* const Munition = World->SpawnActor<AKamikazeDroneNPC>(
		PayloadDroneClass, SpawnTransform.GetLocation(), SpawnTransform.Rotator(), SpawnParams);

	if (!Munition)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Carrier %s] DeployKamikaze: spawn failed for %s"),
			*GetName(), *GetNameSafe(PayloadDroneClass));
		return false;
	}

	// A munition fights the carrier's enemies, so it inherits the carrier's side rather than the
	// payload Blueprint's default. Otherwise a faction carrier drops team-0 drones at its own feet.
	Munition->SetGenericTeamId(GetGenericTeamId());

	// Ignore the hull on the way out: the drone detonates on any hostile contact, and the carrier
	// is not hostile to it, but a blocking hit would still stall the ejection.
	Munition->MoveIgnoreActorAdd(this);
	MoveIgnoreActorAdd(Munition);

	const FVector LaunchDir = GetActorRotation().RotateVector(LaunchDirectionLocal.GetSafeNormal());
	const FVector SpreadDir = (LaunchSpreadAngle > KINDA_SMALL_NUMBER)
		? FMath::VRandCone(LaunchDir, FMath::DegreesToRadians(LaunchSpreadAngle))
		: LaunchDir;

	Munition->LaunchAsHomingMunition(SpreadDir * LaunchSpeed);

	if (!bInfinitePayload)
	{
		--PayloadRemaining;
	}

	if (DeployVFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, DeployVFX, SpawnTransform.GetLocation());
	}
	if (DeploySFX)
	{
		UGameplayStatics::PlaySoundAtLocation(World, DeploySFX, SpawnTransform.GetLocation());
	}

	UE_LOG(LogTemp, Log, TEXT("[Carrier %s] Deployed %s | payload left %d"),
		*GetName(), *Munition->GetName(), PayloadRemaining);

	return true;
}
