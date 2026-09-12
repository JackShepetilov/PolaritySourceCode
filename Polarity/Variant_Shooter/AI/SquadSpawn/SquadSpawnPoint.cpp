// Copyright Epic Games, Inc. All Rights Reserved.

#include "SquadSpawnPoint.h"
#include "SquadLoadout.h"
#include "SquadSpawnSubsystem.h"
#include "Components/SphereComponent.h"

ASquadSpawnPoint::ASquadSpawnPoint()
{
	PrimaryActorTick.bCanEverTick = false;

	// Editor gizmo: shows the scatter radius, never visible in game
	RadiusGizmo = CreateDefaultSubobject<USphereComponent>(TEXT("RadiusGizmo"));
	RadiusGizmo->InitSphereRadius(SpawnRadius);
	RadiusGizmo->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RadiusGizmo->SetHiddenInGame(true);
	RootComponent = RadiusGizmo;
}

void ASquadSpawnPoint::BeginPlay()
{
	Super::BeginPlay();

	if (USquadSpawnSubsystem* Subsystem = GetWorld()->GetSubsystem<USquadSpawnSubsystem>())
	{
		Subsystem->RegisterSpawnPoint(this);
	}

	if (bSpawnOnBeginPlay)
	{
		SpawnSquad(nullptr);
	}
}

void ASquadSpawnPoint::EndPlay(const EEndPlayReason::Type Reason)
{
	if (USquadSpawnSubsystem* Subsystem = GetWorld() ? GetWorld()->GetSubsystem<USquadSpawnSubsystem>() : nullptr)
	{
		Subsystem->UnregisterSpawnPoint(this);
	}

	Super::EndPlay(Reason);
}

int32 ASquadSpawnPoint::SpawnSquad(USquadLoadout* OverrideLoadout, const FVector* Objective)
{
	USquadLoadout* LoadoutToUse = OverrideLoadout ? OverrideLoadout : DefaultLoadout.Get();
	if (!LoadoutToUse)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SQUAD_DEBUG] point '%s' has no loadout to spawn"),
			*PointTag.ToString());
		return 0;
	}

	USquadSpawnSubsystem* Subsystem = GetWorld()->GetSubsystem<USquadSpawnSubsystem>();
	if (!Subsystem)
	{
		return 0;
	}

	return Subsystem->SpawnSquadMembers(GetActorLocation(), SpawnRadius, LoadoutToUse, Objective);
}
