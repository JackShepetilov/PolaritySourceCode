// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "ArenaSpawnPoint.h"
#include "Components/BillboardComponent.h"
#include "Components/ArrowComponent.h"

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
