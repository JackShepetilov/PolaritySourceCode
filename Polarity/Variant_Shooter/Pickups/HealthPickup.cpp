// HealthPickup.cpp
// HP pickup that spawns on prop/drone NPC kills and magnetically flies to the player

#include "HealthPickup.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/DamageTypes/DamageType_DroneExplosion.h"
#include "Variant_Shooter/AI/FlyingDrone.h"
#include "EMFPhysicsProp.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"

AHealthPickup::AHealthPickup()
{
	PrimaryActorTick.bCanEverTick = true;

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

	// If bursting, disable overlaps until the arc flight completes
	if (bIsBursting)
	{
		PickupCollision->SetGenerateOverlapEvents(false);
		MagnetTrigger->SetGenerateOverlapEvents(false);
	}

	// Start lifetime timer
	GetWorld()->GetTimerManager().SetTimer(
		LifetimeTimer, this, &AHealthPickup::OnLifetimeExpired, Lifetime, false);
}

void AHealthPickup::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

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

	if (!MagnetTarget.IsValid())
	{
		return;
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
	AShooterCharacter* Player = Cast<AShooterCharacter>(OtherActor);
	if (!Player || Player->IsDead())
	{
		return;
	}

	// Restore health
	Player->RestoreHealth(HealAmount);

	// Effects
	if (PickupSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, PickupSound, GetActorLocation());
	}

	if (PickupVFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			GetWorld(), PickupVFX, GetActorLocation(),
			FRotator::ZeroRotator, FVector::OneVector,
			true, true, ENCPoolMethod::None);
	}

	Destroy();
}

// ==================== Lifetime ====================

void AHealthPickup::OnLifetimeExpired()
{
	Destroy();
}

// ==================== Static Helpers ====================

void AHealthPickup::SpawnHealthPickups(UWorld* World, TSubclassOf<AHealthPickup> PickupClass,
	const FVector& KillLocation, int32 Count, float ScatterRadius, float FloorOffset)
{
	if (!World || !PickupClass || Count <= 0)
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
