// RunLaunchPoint.cpp

#include "RunLaunchPoint.h"
#include "Components/ArrowComponent.h"

ARunLaunchPoint::ARunLaunchPoint()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

#if WITH_EDITORONLY_DATA
	DirectionArrow = CreateDefaultSubobject<UArrowComponent>(TEXT("DirectionArrow"));
	if (DirectionArrow)
	{
		DirectionArrow->SetupAttachment(SceneRoot);
		DirectionArrow->ArrowColor = FColor::Cyan;
		DirectionArrow->ArrowSize = 3.0f;
		DirectionArrow->bIsScreenSizeScaled = true;
	}
#endif
}

FVector ARunLaunchPoint::GetLaunchVelocity() const
{
	return GetActorForwardVector() * LaunchSpeed;
}
