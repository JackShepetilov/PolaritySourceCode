// HealthPickup.cpp
// HP pickup that spawns on prop/drone NPC kills and magnetically flies to the player

#include "HealthPickup.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/DamageTypes/DamageType_DroneExplosion.h"
#include "Variant_Shooter/AI/FlyingDrone.h"
#include "EMFPhysicsProp.h"
#include "Upgrades/UpgradeManagerComponent.h"
#include "Coop/CoopPlayers.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"

AHealthPickup::AHealthPickup()
{
	PrimaryActorTick.bCanEverTick = true;

	// A pickup is one object shared by the whole team: the server owns it, everybody sees it, and
	// whoever reaches it first takes it. Before this it did not replicate at all, so a pickup spawned
	// on the server -- which is every pickup, since kills and the Melee's decompose are both
	// authority-only -- simply did not exist for anybody else. In a coop game that made the whole of
	// "throw it to a teammate" invisible to the teammate.
	bReplicates = true;

	// The flight is replicated rather than re-simulated per machine. Both the burst arc and the
	// magnet chase depend on which player is being chased and where that player is RIGHT NOW, and
	// two machines do not agree on either closely enough to run it twice and land in the same place.
	// Pickups are few and short-lived, so the bandwidth is not the constraint here; the pickup being
	// somewhere different for each viewer would be.
	SetReplicateMovement(true);

	// Pickup collision (small sphere for actual collection)
	PickupCollision = CreateDefaultSubobject<USphereComponent>(TEXT("PickupCollision"));
	SetRootComponent(PickupCollision);
	PickupCollision->SetSphereRadius(50.0f);
	PickupCollision->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	PickupCollision->SetGenerateOverlapEvents(true);

	// Visual mesh
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(PickupCollision);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Magnet trigger (large sphere for attraction)
	MagnetTrigger = CreateDefaultSubobject<USphereComponent>(TEXT("MagnetTrigger"));
	MagnetTrigger->SetupAttachment(PickupCollision);
	MagnetTrigger->SetSphereRadius(500.0f);
	MagnetTrigger->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	MagnetTrigger->SetGenerateOverlapEvents(true);
}

void AHealthPickup::BeginPlay()
{
	Super::BeginPlay();

	// Sync magnet trigger radius with property
	MagnetTrigger->SetSphereRadius(MagnetRadius);

	// Bind overlap events
	PickupCollision->OnComponentBeginOverlap.AddDynamic(this, &AHealthPickup::OnPickupOverlap);
	MagnetTrigger->OnComponentBeginOverlap.AddDynamic(this, &AHealthPickup::OnMagnetOverlap);

	// NOTE: the burst's overlap suppression is NOT here any more, and could never have worked here.
	// SpawnHealthPickups spawns with a plain SpawnActor and calls InitBurst afterwards, so BeginPlay
	// runs while bIsBursting is still false and this branch never fired. It lives in InitBurst now,
	// which is where the flag actually becomes true.

	// Start lifetime timer
	GetWorld()->GetTimerManager().SetTimer(
		LifetimeTimer, this, &AHealthPickup::OnLifetimeExpired, Lifetime, false);
}

void AHealthPickup::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Everything below MOVES this actor or decides that it has been taken, and both belong to the
	// authority now that the movement replicates. A client running this as well would fight the
	// replicated position every frame and would reach its own conclusion about who collected it.
	if (!HasAuthority())
	{
		return;
	}

	// --- Burst arc flight phase ---
	if (bIsBursting)
	{
		BurstElapsedTime += DeltaTime;
		const float Alpha = FMath::Clamp(BurstElapsedTime / BurstDuration, 0.0f, 1.0f);

		// Ease-out curve for snappy Doom Eternal feel: fast launch, decelerating landing
		const float EasedAlpha = 1.0f - FMath::Square(1.0f - Alpha);

		// XY: linear interpolation from start to target
		const FVector FlatPos = FMath::Lerp(BurstStartLocation, BurstTargetLocation, EasedAlpha);

		// Z: linear lerp + parabolic arc on top (peaks at midpoint)
		const float LinearZ = FMath::Lerp(BurstStartLocation.Z, BurstTargetLocation.Z, EasedAlpha);
		const float ArcZ = BurstArcHeight * 4.0f * Alpha * (1.0f - Alpha);

		SetActorLocation(FVector(FlatPos.X, FlatPos.Y, LinearZ + ArcZ));

		if (Alpha >= 1.0f)
		{
			OnBurstComplete();
		}
		return;
	}

	// Proximity poll, and it is not a belt-and-braces addition to the overlap events -- it is what
	// actually makes collection reliable. A pickup that is BORN already overlapping somebody (a prop
	// that broke against a standing player is the everyday case) registers that overlap before
	// BeginPlay binds the delegates, so OnMagnetOverlap never fires for them, and it never fires
	// later either: the overlap is already in the cached list and only a fresh BEGIN would report
	// it. Standing still meant standing on a pickup that ignored you until you stepped off and back.
	//
	// Distance is asked once a frame instead. Cheap next to what it fixes, and it cannot be fooled
	// by event timing.
	AcquireMagnetTargetByProximity();

	if (!MagnetTarget.IsValid())
	{
		return;
	}

	// Close enough to have been collected. The overlap callback still exists and still fires for the
	// ordinary case; this is the same decision reached without it.
	if (AShooterCharacter* Player = MagnetTarget.Get())
	{
		const float PickupRadius = PickupCollision ? PickupCollision->GetScaledSphereRadius() : 50.0f;
		if (FVector::Dist(Player->GetActorLocation(), GetActorLocation()) <= PickupRadius)
		{
			TryCollect(Player);
			return;
		}
	}

	// Track elapsed time since magnet activation (reuse CurrentVelocity.X as timer)
	CurrentVelocity.X += DeltaTime;

	// Direct pursuit: always move straight toward the player, no inertia
	const FVector TargetLocation = MagnetTarget->GetActorLocation();
	const FVector ToTarget = TargetLocation - GetActorLocation();
	const float Distance = ToTarget.Size();

	if (Distance < 1.0f)
	{
		return;
	}

	// Speed ramps up over time: starts slow, reaches MagnetSpeed after ~0.5s
	const float SpeedAlpha = FMath::Clamp(CurrentVelocity.X * MagnetAcceleration / MagnetSpeed, 0.0f, 1.0f);
	const float CurrentSpeed = FMath::Lerp(MagnetSpeed * 0.1f, MagnetSpeed, SpeedAlpha * SpeedAlpha);

	// Move directly toward player, clamped to not overshoot
	const float MoveDistance = FMath::Min(CurrentSpeed * DeltaTime, Distance);
	const FVector NewLocation = GetActorLocation() + (ToTarget / Distance) * MoveDistance;
	SetActorLocation(NewLocation);
}

// ==================== Burst Flight ====================

void AHealthPickup::InitBurst(const FVector& TargetLocation)
{
	bIsBursting = true;
	BurstStartLocation = GetActorLocation();
	BurstTargetLocation = TargetLocation;
	BurstElapsedTime = 0.0f;

	// Quiet for the length of the arc, so a pickup does not get collected in mid-flight the instant
	// it is born inside whoever it spawned on. Done HERE rather than in BeginPlay because this is
	// the moment the burst actually starts: BeginPlay has already run by the time the spawner calls
	// this, with the flag still false, so the copy of this that used to live there was dead code.
	if (PickupCollision)
	{
		PickupCollision->SetGenerateOverlapEvents(false);
	}
	if (MagnetTrigger)
	{
		MagnetTrigger->SetGenerateOverlapEvents(false);
	}
}

void AHealthPickup::OnBurstComplete()
{
	bIsBursting = false;
	SetActorLocation(BurstTargetLocation);

	// Re-enable overlaps now that we've landed
	PickupCollision->SetGenerateOverlapEvents(true);
	MagnetTrigger->SetGenerateOverlapEvents(true);

	// Force overlap check — player might already be standing on the landing spot
	PickupCollision->UpdateOverlaps();
	MagnetTrigger->UpdateOverlaps();
}

// ==================== Overlap Callbacks ====================

void AHealthPickup::OnMagnetOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (MagnetTarget.IsValid())
	{
		return; // Already tracking
	}

	AShooterCharacter* Player = Cast<AShooterCharacter>(OtherActor);
	if (Player && !Player->IsDead())
	{
		MagnetTarget = Player;
	}
}

void AHealthPickup::OnPickupOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	TryCollect(Cast<AShooterCharacter>(OtherActor));
}

void AHealthPickup::AcquireMagnetTargetByProximity()
{
	if (MagnetTarget.IsValid() || MagnetRadius <= 0.0f)
	{
		return;
	}

	// Nearest of ALL players, not "the player": this is a coop game and the pickup belongs to
	// whoever reaches it.
	APawn* Nearest = CoopPlayers::GetNearest(GetWorld(), GetActorLocation());
	AShooterCharacter* Player = Cast<AShooterCharacter>(Nearest);
	if (!Player || Player->IsDead())
	{
		return;
	}

	if (FVector::Dist(Player->GetActorLocation(), GetActorLocation()) <= MagnetRadius)
	{
		MagnetTarget = Player;
	}
}

void AHealthPickup::TryCollect(AShooterCharacter* Player)
{
	if (!Player || Player->IsDead() || bCollected)
	{
		return;
	}

	// Health is the server's to write. A client reaching this would heal a copy of itself that the
	// next replication update overwrites, and would destroy its own copy of a pickup still standing
	// on every other machine.
	if (!HasAuthority())
	{
		return;
	}

	// Two ways in -- the overlap event and the proximity poll -- and on the frame they agree this
	// would otherwise pay out twice.
	bCollected = true;

	// If at full HP, notify upgrade system instead of healing
	if (Player->GetCurrentHP() >= Player->GetMaxHP())
	{
		if (UUpgradeManagerComponent* UpgradeManager = Player->GetUpgradeManager())
		{
			UpgradeManager->NotifyHealthPickupCollectedAtFullHP();
		}
		// Don't call RestoreHealth (would do nothing anyway)
	}
	else
	{
		// Restore health
		Player->RestoreHealth(HealAmount);
	}

	// Notify tutorial system about health pickup collection
	Player->NotifyHealthPickupCollected();

	// Effects — always play regardless of full HP or not, and on every machine. Played through a
	// multicast rather than here: this function now only runs on the server, so a local call would
	// mean the person who actually picked it up hears nothing.
	Multicast_PlayCollected(GetActorLocation());

	// Destruction replicates by itself, which is what takes the pickup off every screen.
	Destroy();
}

void AHealthPickup::Multicast_PlayCollected_Implementation(FVector Location)
{
	if (PickupSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, PickupSound, Location);
	}

	if (PickupVFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			GetWorld(), PickupVFX, Location,
			FRotator::ZeroRotator, FVector::OneVector,
			true, true, ENCPoolMethod::None);
	}
}

// ==================== Lifetime ====================

void AHealthPickup::OnLifetimeExpired()
{
	// The timer runs on every machine, the destruction is the server's. A client destroying its own
	// copy would take the pickup off its screen while it still exists for everybody else.
	if (HasAuthority())
	{
		Destroy();
	}
}

// ==================== Static Helpers ====================

void AHealthPickup::SpawnHealthPickups(UWorld* World, TSubclassOf<AHealthPickup> PickupClass,
	const FVector& KillLocation, int32 Count, float ScatterRadius, float FloorOffset)
{
	if (!World || !PickupClass || Count <= 0)
	{
		return;
	}

	// Spawned on the authority only, and replicated out from there. Every caller today is already
	// inside an authority check; stating it here as well means the next one cannot get it wrong and
	// end up with pickups that exist on one machine.
	if (!World->GetAuthGameMode() && World->GetNetMode() == NM_Client)
	{
		return;
	}

	// --- Step 1: Find the floor below the kill location ---
	// Trace a long distance down to find solid ground
	FHitResult FloorHit;
	const FVector TraceStart = KillLocation;
	const FVector TraceEnd = KillLocation - FVector(0.0f, 0.0f, 5000.0f);

	FCollisionQueryParams TraceParams;
	TraceParams.bTraceComplex = false;
	TraceParams.AddIgnoredActor(nullptr);

	float FloorZ = KillLocation.Z; // fallback: use kill location Z
	if (World->LineTraceSingleByChannel(FloorHit, TraceStart, TraceEnd, ECC_Visibility, TraceParams))
	{
		FloorZ = FloorHit.ImpactPoint.Z;
	}

	// The base spawn height: floor + offset
	const float SpawnZ = FloorZ + FloorOffset;
	const FVector BaseSpawnPoint(KillLocation.X, KillLocation.Y, SpawnZ);

	// --- Step 2: Scatter pickups around the base point ---
	const float AngleStep = 360.0f / FMath::Max(Count, 1);
	// Add a random rotation so the pattern isn't always aligned to world axes
	const float RandomBaseAngle = FMath::FRandRange(0.0f, 360.0f);

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	for (int32 i = 0; i < Count; ++i)
	{
		FVector LandingLocation;

		if (Count == 1)
		{
			// Single pickup: land directly at base point, no scatter
			LandingLocation = BaseSpawnPoint;
		}
		else
		{
			// Calculate scatter direction in a circle
			const float Angle = RandomBaseAngle + AngleStep * i;
			const float AngleRad = FMath::DegreesToRadians(Angle);
			const FVector ScatterDir(FMath::Cos(AngleRad), FMath::Sin(AngleRad), 0.0f);

			// Desired scatter position
			FVector DesiredLocation = BaseSpawnPoint + ScatterDir * ScatterRadius;

			// --- Step 3: Wall check — trace from base to desired to avoid spawning inside walls ---
			FHitResult WallHit;
			if (World->LineTraceSingleByChannel(WallHit, BaseSpawnPoint, DesiredLocation, ECC_Visibility, TraceParams))
			{
				// Pull back from the wall by a small margin
				DesiredLocation = WallHit.ImpactPoint - ScatterDir * 20.0f;
			}

			// --- Step 4: Floor trace at the scattered position ---
			// The scattered position might be over a ledge or pit, re-trace floor
			FHitResult ScatterFloorHit;
			const FVector ScatterTraceStart = FVector(DesiredLocation.X, DesiredLocation.Y, KillLocation.Z + 200.0f);
			const FVector ScatterTraceEnd = FVector(DesiredLocation.X, DesiredLocation.Y, KillLocation.Z - 5000.0f);

			if (World->LineTraceSingleByChannel(ScatterFloorHit, ScatterTraceStart, ScatterTraceEnd, ECC_Visibility, TraceParams))
			{
				DesiredLocation.Z = ScatterFloorHit.ImpactPoint.Z + FloorOffset;
			}

			LandingLocation = DesiredLocation;
		}

		// Spawn at the kill center, then burst-fly to the landing spot
		AHealthPickup* Pickup = World->SpawnActor<AHealthPickup>(PickupClass, KillLocation, FRotator::ZeroRotator, SpawnParams);
		if (Pickup)
		{
			Pickup->InitBurst(LandingLocation);
		}
	}
}

bool AHealthPickup::ShouldDropHealth(TSubclassOf<UDamageType> KillingDamageType, AActor* KillingDamageCauser)
{
	UE_LOG(LogTemp, Warning, TEXT("[ShouldDropHealth] DamageType=%s, DamageCauser=%s (Class=%s)"),
		KillingDamageType ? *KillingDamageType->GetName() : TEXT("NULL"),
		KillingDamageCauser ? *KillingDamageCauser->GetName() : TEXT("NULL"),
		KillingDamageCauser ? *KillingDamageCauser->GetClass()->GetName() : TEXT("NULL"));

	// Prop kills (collision or explosion) — DamageCauser is the prop itself
	if (Cast<AEMFPhysicsProp>(KillingDamageCauser))
	{
		UE_LOG(LogTemp, Warning, TEXT("[ShouldDropHealth] -> TRUE (prop kill)"));
		return true;
	}

	// Drone kills (kinetic collision, wall slam self-destruct, or explosion)
	if (Cast<AFlyingDrone>(KillingDamageCauser))
	{
		UE_LOG(LogTemp, Warning, TEXT("[ShouldDropHealth] -> TRUE (drone kill)"));
		return true;
	}

	// Drone explosion kills — identified by DamageType_DroneExplosion
	// (DamageCauser is PlayerPawn for friendly-fire bypass, so check DamageType)
	if (KillingDamageType && KillingDamageType->IsChildOf(UDamageType_DroneExplosion::StaticClass()))
	{
		UE_LOG(LogTemp, Warning, TEXT("[ShouldDropHealth] -> TRUE (drone explosion type)"));
		return true;
	}

	// Everything else: no health drop
	UE_LOG(LogTemp, Warning, TEXT("[ShouldDropHealth] -> FALSE (no matching condition)"));
	return false;
}
