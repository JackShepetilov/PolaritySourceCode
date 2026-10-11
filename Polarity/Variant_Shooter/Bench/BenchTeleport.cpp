// BenchTeleport.cpp

#include "Variant_Shooter/Bench/BenchTeleport.h"

#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Coop/CoopPlayers.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "UObject/ConstructorHelpers.h"
#include "Variant_Shooter/ShootableButtonComponent.h"

ABenchTeleport::ABenchTeleport()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	// Toggle mode with a cooldown: it works every time and never needs a reset.
	Button = CreateDefaultSubobject<UShootableButtonComponent>(TEXT("Button"));
	Button->SetupAttachment(SceneRoot);
	Button->bAutoDisableOnPress = false;
	Button->ActivationCooldown = 0.6f;

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
	Label->SetWorldSize(20.0f);
	Label->SetTextRenderColor(FColor(120, 220, 255));
	Label->SetText(FText::FromString(TEXT("TELEPORT")));
}

void ABenchTeleport::BeginPlay()
{
	Super::BeginPlay();

	if (Label)
	{
		Label->SetText(FText::FromString(Title));
	}
	if (HasAuthority() && Button)
	{
		Button->OnButtonPressed.AddDynamic(this, &ABenchTeleport::HandleButtonPressed);
	}
}

void ABenchTeleport::HandleButtonPressed(UShootableButtonComponent* Pressed, AActor* Activator)
{
	if (!HasAuthority() || !Destination)
	{
		return;
	}

	// The activator is whatever the hit named: the shooter, its weapon or its projectile. Walk up to
	// the pawn behind it; a shot nobody can be found behind moves nobody.
	APawn* Pawn = Cast<APawn>(Activator);
	if (!Pawn && Activator)
	{
		Pawn = Activator->GetInstigator();
	}
	if (!Pawn && Activator)
	{
		Pawn = Cast<APawn>(Activator->GetOwner());
	}
	if (!Pawn || !CoopPlayers::IsPlayer(Pawn))
	{
		UE_LOG(LogTemp, Log, TEXT("[BENCH] teleport '%s': no player behind %s"), *Title, *GetNameSafe(Activator));
		return;
	}

	const FRotator Facing = Destination->GetActorRotation();
	Pawn->TeleportTo(Destination->GetActorLocation(), Facing, /*bIsATest*/ false, /*bNoCheck*/ true);
	if (AController* const Controller = Pawn->GetController())
	{
		Controller->SetControlRotation(Facing);
	}

	UE_LOG(LogTemp, Log, TEXT("[BENCH] teleport '%s': %s -> %s"), *Title, *Pawn->GetName(), *Destination->GetActorLocation().ToCompactString());
}
