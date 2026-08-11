#include "ArenaGrassExclusionVolume.h"
#include "Components/BoxComponent.h"

AArenaGrassExclusionVolume::AArenaGrassExclusionVolume()
{
	PrimaryActorTick.bCanEverTick = false;
	ExclusionBox = CreateDefaultSubobject<UBoxComponent>(TEXT("ExclusionBox"));
	SetRootComponent(ExclusionBox);
	ExclusionBox->SetBoxExtent(FVector(1000.f, 1000.f, 10000.f));
	ExclusionBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ExclusionBox->SetGenerateOverlapEvents(false);
	ExclusionBox->SetCanEverAffectNavigation(false);
	ExclusionBox->SetHiddenInGame(true);
	ExclusionBox->ShapeColor = FColor(64, 220, 80);
}

FBox AArenaGrassExclusionVolume::GetWorldExclusionBox() const
{
	return ExclusionBox ? ExclusionBox->Bounds.GetBox() : FBox(ForceInit);
}
