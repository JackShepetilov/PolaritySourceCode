// RunStartButton.cpp

#include "Variant_Shooter/Run/RunStartButton.h"

#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"
#include "Variant_Shooter/Run/RunRoom.h"
#include "Variant_Shooter/ShootableButtonComponent.h"

ARunStartButton::ARunStartButton()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	// Toggle mode with a cooldown, like the bench teleports: it works every time and never needs a reset.
	Button = CreateDefaultSubobject<UShootableButtonComponent>(TEXT("Button"));
	Button->SetupAttachment(SceneRoot);
	Button->bAutoDisableOnPress = false;
	Button->ActivationCooldown = 1.0f;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded() && Button->GetButtonMesh())
	{
		Button->GetButtonMesh()->SetStaticMesh(Cube.Object);
		Button->GetButtonMesh()->SetRelativeScale3D(FVector(0.15f, 0.5f, 0.5f));
	}

	Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label"));
	Label->SetupAttachment(SceneRoot);
	Label->SetRelativeLocation(FVector(10.0f, 0.0f, 45.0f));
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetVerticalAlignment(EVRTA_TextBottom);
	Label->SetWorldSize(24.0f);
	Label->SetTextRenderColor(FColor(140, 255, 160));
	Label->SetText(FText::FromString(TEXT("RUN START")));
}

void ARunStartButton::BeginPlay()
{
	Super::BeginPlay();

	if (Label)
	{
		Label->SetText(FText::FromString(Title));
	}
	if (HasAuthority() && Button)
	{
		Button->OnButtonPressed.AddDynamic(this, &ARunStartButton::HandleButtonPressed);
	}
}

void ARunStartButton::HandleButtonPressed(UShootableButtonComponent* Pressed, AActor* Activator)
{
	if (HasAuthority())
	{
		ARunRoom::StartRunAt(GetWorld(), FirstRoom);
	}
}
