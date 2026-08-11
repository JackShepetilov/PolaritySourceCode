#include "BiomeArenaAnchor.h"
#include "Components/ArrowComponent.h"
#include "Components/SceneComponent.h"

ABiomeArenaAnchor::ABiomeArenaAnchor()
{
	PrimaryActorTick.bCanEverTick = false;
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
#if WITH_EDITORONLY_DATA
	DirectionArrow = CreateEditorOnlyDefaultSubobject<UArrowComponent>(TEXT("DirectionArrow"));
	if (DirectionArrow)
	{
		DirectionArrow->SetupAttachment(SceneRoot);
		DirectionArrow->ArrowColor = FColor(255, 180, 0);
		DirectionArrow->ArrowSize = 2.f;
	}
#endif
}
