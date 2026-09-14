// MetalPickup.cpp

#include "MetalPickup.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/ShooterPlayerState.h"
#include "Coop/CoopPlayers.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

AMetalPickup::AMetalPickup()
{
	PrimaryActorTick.bCanEverTick = true;

	// Same deal as AHealthPickup: the server owns the pile and moves it, everybody else watches.
	// The magnet chases one specific player, and two machines would not agree on where they are.
	bReplicates = true;
	SetReplicateMovement(true);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetRelativeScale3D(FVector(0.2f));

	// A plain cube until the pile gets real art, so the C++ class works on its own: placed on a
	// level or dropped by UMetalDropComponent without a blueprint in between.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		Mesh->SetStaticMesh(CubeMesh.Object);
	}
}

void AMetalPickup::BeginPlay()
{
	Super::BeginPlay();

	// Neither placed stock nor earned rewards expire.
}

void AMetalPickup::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AMetalPickup, Amount);
}

void AMetalPickup::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Moving the pile and deciding who took it are both the server's.
	if (!HasAuthority())
	{
		return;
	}

	if (bIsBursting)
	{
		BurstElapsedTime += DeltaTime;
		const float Alpha = FMath::Clamp(BurstElapsedTime / FMath::Max(BurstDuration, 0.01f), 0.0f, 1.0f);
		const float EasedAlpha = 1.0f - FMath::Square(1.0f - Alpha);

		const FVector FlatPos = FMath::Lerp(BurstStartLocation, BurstTargetLocation, EasedAlpha);
		const float ArcZ = BurstArcHeight * 4.0f * Alpha * (1.0f - Alpha);
		SetActorLocation(FVector(FlatPos.X, FlatPos.Y, FlatPos.Z + ArcZ));

		if (Alpha >= 1.0f)
		{
			bIsBursting = false;
			SetActorLocation(BurstTargetLocation);
		}
		return;
	}

	// A target that died or filled up on the way lets go, so the pile settles where it is and the
	// next player with room can have it.
	AShooterCharacter* Player = MagnetTarget.Get();
	if (Player && (Player->IsDead() || !HasRoom(Player)))
	{
		DropMagnetTarget();
		Player = nullptr;
	}

	if (!Player)
	{
		AcquireMagnetTarget();
		Player = MagnetTarget.Get();
		if (!Player)
		{
			return;
		}
	}

	const FVector ToTarget = Player->GetActorLocation() - GetActorLocation();
	const float Distance = ToTarget.Size();
	if (Distance <= PickupRadius)
	{
		TryCollect(Player);
		return;
	}

	// Starts slow and ramps up, the same curve the health pickup uses.
	MagnetElapsed += DeltaTime;
	// Distant air kills must arrive promptly too; near the player use the ordinary magnet speed.
	const float FlightSpeed = bAutoCollectDrop ? FMath::Max(MagnetSpeed, Distance / 1.5f) : MagnetSpeed;
	const float SafeSpeed = FMath::Max(FlightSpeed, 1.0f);
	const float SpeedAlpha = FMath::Clamp(MagnetElapsed * MagnetAcceleration / FMath::Max(MagnetSpeed, 1.0f), 0.0f, 1.0f);
	const float CurrentSpeed = FMath::Lerp(SafeSpeed * 0.1f, SafeSpeed, SpeedAlpha * SpeedAlpha);
	const float MoveDistance = FMath::Min(CurrentSpeed * DeltaTime, Distance);
	SetActorLocation(GetActorLocation() + (ToTarget / Distance) * MoveDistance);
}

void AMetalPickup::InitBurst(const FVector& TargetLocation)
{
	if (!HasAuthority())
	{
		return;
	}
	bAutoCollectDrop = true;
	bAlwaysRelevant = true;
	bIsBursting = true;
	BurstStartLocation = GetActorLocation();
	BurstTargetLocation = TargetLocation;
	BurstElapsedTime = 0.0f;

	GetWorldTimerManager().ClearTimer(LifetimeTimer);
	SetLifeSpan(0.0f);
	UE_LOG(LogTemp, Log, TEXT("[METAL_DEBUG] %s auto-collect reward: %d metal"), *GetName(), Amount);
}

bool AMetalPickup::HasRoom(const AShooterCharacter* Player)
{
	const AShooterPlayerState* State = Player ? Player->GetPlayerState<AShooterPlayerState>() : nullptr;
	return State && State->GetMetalRoom() > 0;
}

void AMetalPickup::AcquireMagnetTarget()
{
	if (!bAutoCollectDrop && MagnetRadius <= 0.0f)
	{
		return;
	}

	// Server side, so this is the whole team.
	TArray<APawn*> Players;
	CoopPlayers::GetAll(GetWorld(), Players);

	const FVector Here = GetActorLocation();
	AShooterCharacter* Best = nullptr;
	float BestDistSq = bAutoCollectDrop ? TNumericLimits<float>::Max() : FMath::Square(MagnetRadius);

	for (APawn* Pawn : Players)
	{
		AShooterCharacter* Candidate = Cast<AShooterCharacter>(Pawn);
		if (!Candidate || Candidate->IsDead() || !HasRoom(Candidate))
		{
			continue;
		}

		const float DistSq = FVector::DistSquared(Candidate->GetActorLocation(), Here);
		if (DistSq <= BestDistSq)
		{
			BestDistSq = DistSq;
			Best = Candidate;
		}
	}

	if (Best)
	{
		MagnetTarget = Best;
		MagnetElapsed = 0.0f;
	}
}

void AMetalPickup::DropMagnetTarget()
{
	MagnetTarget.Reset();
	MagnetElapsed = 0.0f;
}

void AMetalPickup::TryCollect(AShooterCharacter* Player)
{
	if (!HasAuthority() || !Player || Player->IsDead() || Amount <= 0)
	{
		return;
	}

	AShooterPlayerState* State = Player->GetPlayerState<AShooterPlayerState>();
	const int32 Taken = State ? State->AddMetal(Amount) : 0;
	if (Taken <= 0)
	{
		DropMagnetTarget();
		return;
	}

	Amount -= Taken;
	Multicast_PlayCollected(GetActorLocation());

	if (Amount <= 0)
	{
		Destroy();
		return;
	}

	// Part of it fit. The player is full now, so the magnet lets go and the rest waits here.
	UE_LOG(LogTemp, Log, TEXT("[METAL_DEBUG] %s took %d, %d left on the floor"), *GetNameSafe(State), Taken, Amount);
	DropMagnetTarget();
}

void AMetalPickup::Multicast_PlayCollected_Implementation(FVector Location)
{
	if (PickupSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, PickupSound, Location);
	}

	if (PickupVFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			GetWorld(), PickupVFX, Location, FRotator::ZeroRotator, FVector::OneVector,
			true, true, ENCPoolMethod::None);
	}
}

void AMetalPickup::OnLifetimeExpired()
{
	// Also harmless for a timer left over from an older live session.
}
