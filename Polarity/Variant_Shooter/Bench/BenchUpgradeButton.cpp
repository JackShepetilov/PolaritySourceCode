// BenchUpgradeButton.cpp

#include "Variant_Shooter/Bench/BenchUpgradeButton.h"

#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Coop/CoopPlayers.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Pawn.h"
#include "UObject/ConstructorHelpers.h"
#include "Upgrades/UpgradeDefinition.h"
#include "Upgrades/UpgradeManagerComponent.h"
#include "Variant_Shooter/ShootableButtonComponent.h"
#include "Variant_Shooter/ShooterCharacter.h"

ABenchUpgradeButton::ABenchUpgradeButton()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	Button = CreateDefaultSubobject<UShootableButtonComponent>(TEXT("Button"));
	Button->SetupAttachment(SceneRoot);
	Button->bAutoDisableOnPress = false;
	Button->ActivationCooldown = 0.4f;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded() && Button->GetButtonMesh())
	{
		Button->GetButtonMesh()->SetStaticMesh(Cube.Object);
		Button->GetButtonMesh()->SetRelativeScale3D(FVector(0.12f, 0.3f, 0.3f));
	}

	Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label"));
	Label->SetupAttachment(SceneRoot);
	Label->SetRelativeLocation(FVector(8.0f, 0.0f, 20.0f));
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetVerticalAlignment(EVRTA_TextBottom);
	Label->SetWorldSize(11.0f);
	Label->SetText(FText::FromString(TEXT("UPGRADE")));
}

void ABenchUpgradeButton::BeginPlay()
{
	Super::BeginPlay();

	ShowLevel(0);
	if (HasAuthority() && Button)
	{
		Button->OnButtonPressed.AddDynamic(this, &ABenchUpgradeButton::HandleButtonPressed);
	}
}

void ABenchUpgradeButton::ShowLevel(int32 Level)
{
	if (!Label)
	{
		return;
	}

	FString Name = Upgrade ? Upgrade->DisplayName.ToString() : FString(TEXT("no upgrade"));
	if (Name.IsEmpty() && Upgrade)
	{
		Name = Upgrade->GetName();
	}
	const int32 MaxLevel = Upgrade ? FMath::Max(1, Upgrade->MaxLevel) : 1;

	Label->SetText(FText::FromString(Level > 0
		? (MaxLevel > 1 ? FString::Printf(TEXT("%s\nON %d/%d"), *Name, Level, MaxLevel) : FString::Printf(TEXT("%s\nON"), *Name))
		: FString::Printf(TEXT("%s\noff"), *Name)));
	// Dark when off: the bench walls are pale, and a grey label on them could not be read from the start point.
	Label->SetTextRenderColor(Level > 0 ? FColor(0, 120, 0) : FColor(35, 25, 15));
}

void ABenchUpgradeButton::HandleButtonPressed(UShootableButtonComponent* Pressed, AActor* Activator)
{
	if (!HasAuthority() || !Upgrade)
	{
		return;
	}

	// The activator is whatever the hit named: the shooter, its weapon or its round.
	APawn* Pawn = Cast<APawn>(Activator);
	if (!Pawn && Activator)
	{
		Pawn = Activator->GetInstigator();
	}
	if (!Pawn && Activator)
	{
		Pawn = Cast<APawn>(Activator->GetOwner());
	}
	AShooterCharacter* const Shooter = CoopPlayers::IsPlayer(Pawn) ? Cast<AShooterCharacter>(Pawn) : nullptr;
	UUpgradeManagerComponent* const Manager = Shooter ? Shooter->GetUpgradeManager() : nullptr;
	if (!Manager)
	{
		UE_LOG(LogTemp, Log, TEXT("[BENCH] upgrade '%s': no player behind %s"), *GetNameSafe(Upgrade), *GetNameSafe(Activator));
		return;
	}

	const FGameplayTag Tag = Upgrade->UpgradeTag;
	const int32 Before = Manager->GetUpgradeLevel(Tag);
	if (Before >= FMath::Max(1, Upgrade->MaxLevel))
	{
		Manager->RemoveUpgrade(Tag);
	}
	else
	{
		Manager->GrantUpgradeEverywhere(Upgrade);
	}

	const int32 After = Manager->GetUpgradeLevel(Tag);
	ShowLevel(After);
	UE_LOG(LogTemp, Log, TEXT("[BENCH] upgrade '%s' for %s: level %d -> %d"), *Tag.ToString(), *Shooter->GetName(), Before, After);
}
