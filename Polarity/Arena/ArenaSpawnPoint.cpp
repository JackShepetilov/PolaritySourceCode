// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "ArenaSpawnPoint.h"
#include "Polarity/Variant_Shooter/AI/ShooterNPC.h"
#include "Polarity/Variant_Shooter/AI/FlyingDrone.h"
#include "Components/BillboardComponent.h"
#include "Components/ArrowComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "NavigationSystem.h"

AArenaSpawnPoint::AArenaSpawnPoint()
{
	PrimaryActorTick.bCanEverTick = false;

	// Root scene component
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

#if WITH_EDITORONLY_DATA
	EditorSprite = CreateEditorOnlyDefaultSubobject<UBillboardComponent>(TEXT("EditorSprite"));
	if (EditorSprite)
	{
		EditorSprite->SetupAttachment(Root);
		EditorSprite->SetHiddenInGame(true);
	}

	EditorArrow = CreateEditorOnlyDefaultSubobject<UArrowComponent>(TEXT("EditorArrow"));
	if (EditorArrow)
	{
		EditorArrow->SetupAttachment(Root);
		EditorArrow->SetHiddenInGame(true);
		EditorArrow->ArrowColor = FColor::Green;
		EditorArrow->ArrowSize = 1.0f;
	}
#endif
}

bool AArenaSpawnPoint::IsClassAllowed(TSubclassOf<AShooterNPC> NPCClass) const
{
	if (ExcludedNPCClasses.Num() == 0 || !NPCClass)
	{
		return true;
	}

	for (const TSubclassOf<AShooterNPC>& Excluded : ExcludedNPCClasses)
	{
		if (Excluded && NPCClass->IsChildOf(Excluded))
		{
			return false;
		}
	}
	return true;
}

FTransform AArenaSpawnPoint::GetSpawnTransform(bool bForAirUnit) const
{
	FTransform Result = GetActorTransform();

	if (bForAirUnit && bAirSpawn)
	{
		FVector Location = Result.GetLocation();
		Location.Z += AirSpawnHeight;
		Result.SetLocation(Location);
	}

	return Result;
}

FTransform AArenaSpawnPoint::ResolveSpawnTransformFor(TSubclassOf<AShooterNPC> NPCClass) const
{
	if (!NPCClass)
	{
		return FTransform::Identity;
	}

	const bool bIsFlyingUnit = NPCClass->IsChildOf(AFlyingDrone::StaticClass());
	FTransform Result = GetSpawnTransform(bIsFlyingUnit);
	FVector SpawnLocation = Result.GetLocation();

	if (!bIsFlyingUnit)
	{
		const ACharacter* CDO = NPCClass->GetDefaultObject<ACharacter>();
		const float CapsuleHalfHeight = (CDO ? CDO->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() : 96.0f) + 10.0f;

		UWorld* World = GetWorld();
		UNavigationSystemV1* NavSys = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
		if (NavSys)
		{
			FNavLocation NavResult;
			const FVector ProjectionExtent(50.0f, 50.0f, 500.0f);
			if (NavSys->ProjectPointToNavigation(SpawnLocation, NavResult, ProjectionExtent))
			{
				SpawnLocation.X = NavResult.Location.X;
				SpawnLocation.Y = NavResult.Location.Y;
				SpawnLocation.Z = NavResult.Location.Z + CapsuleHalfHeight;
			}
			else if (World)
			{
				FHitResult GroundHit;
				FCollisionQueryParams TraceParams;
				const FVector TraceStart = SpawnLocation + FVector(0.0f, 0.0f, 200.0f);
				const FVector TraceEnd = SpawnLocation - FVector(0.0f, 0.0f, 500.0f);
				if (World->LineTraceSingleByChannel(GroundHit, TraceStart, TraceEnd, ECC_WorldStatic, TraceParams))
				{
					SpawnLocation.Z = GroundHit.ImpactPoint.Z + CapsuleHalfHeight;
				}
			}
		}
	}

	Result.SetLocation(SpawnLocation);
	return Result;
}
