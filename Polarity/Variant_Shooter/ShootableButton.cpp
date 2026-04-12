// ShootableButton.cpp

#include "ShootableButton.h"
#include "ShootableButtonComponent.h"

AShootableButton::AShootableButton()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	ButtonComponent = CreateDefaultSubobject<UShootableButtonComponent>(TEXT("ButtonComponent"));
	ButtonComponent->SetupAttachment(SceneRoot);
}
