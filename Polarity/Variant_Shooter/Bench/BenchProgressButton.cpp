// BenchProgressButton.cpp

#include "Variant_Shooter/Bench/BenchProgressButton.h"

#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Coop/CoopPlayers.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Pawn.h"
#include "UObject/ConstructorHelpers.h"
#include "Variant_Shooter/Inventory/InventoryComponent.h"
#include "Variant_Shooter/Progression/RunLevelComponent.h"
#include "Variant_Shooter/Progression/WeaponMasteryComponent.h"
#include "Variant_Shooter/Progression/WeaponMasteryDefinition.h"
#include "Variant_Shooter/ShootableButtonComponent.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"

ABenchProgressButton::ABenchProgressButton()
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
	Label->SetWorldSize(14.0f);
	Label->SetTextRenderColor(FColor(35, 25, 15));
}

void ABenchProgressButton::BeginPlay()
{
	Super::BeginPlay();

	ShowState(nullptr);
	if (HasAuthority() && Button)
	{
		Button->OnButtonPressed.AddDynamic(this, &ABenchProgressButton::HandleButtonPressed);
	}
}

FString ABenchProgressButton::GetTitle() const
{
	switch (Action)
	{
	case EBenchProgressAction::MasteryUp:        return TEXT("GUN MASTERY +1");
	case EBenchProgressAction::MasteryDown:      return TEXT("GUN MASTERY -1");
	case EBenchProgressAction::MasteryReset:     return TEXT("GUN MASTERY 0");
	case EBenchProgressAction::MasteryMax:       return TEXT("GUN MASTERY MAX");
	case EBenchProgressAction::RunLevelUp:       return TEXT("RUN LEVEL +1");
	case EBenchProgressAction::RunLevelReset:    return TEXT("RUN LEVEL 0");
	case EBenchProgressAction::GadgetKindsTwo:   return TEXT("GADGET KINDS 2");
	case EBenchProgressAction::GadgetKindsThree: return TEXT("GADGET KINDS 3 (perk)");
	}
	return TEXT("?");
}

void ABenchProgressButton::ShowState(const AShooterCharacter* Shooter)
{
	if (!Label)
	{
		return;
	}

	FString State;
	if (Shooter)
	{
		switch (Action)
		{
		case EBenchProgressAction::MasteryUp:
		case EBenchProgressAction::MasteryDown:
		case EBenchProgressAction::MasteryReset:
		case EBenchProgressAction::MasteryMax:
		{
			const AShooterWeapon* const Weapon = Shooter->GetCurrentWeapon();
			const UWeaponMasteryComponent* const Mastery = UWeaponMasteryComponent::FindFor(Shooter);
			const UWeaponMasteryDefinition* const Def = Weapon ? Weapon->MasteryDefinition.Get() : nullptr;
			State = Def && Mastery
				? FString::Printf(TEXT("%s: %d / %d"), *Def->GetName().Replace(TEXT("DA_Mastery_"), TEXT("")), Mastery->GetLevel(Def), Def->Steps.Num())
				: FString(TEXT("no mastery on this gun"));
			break;
		}
		case EBenchProgressAction::RunLevelUp:
		case EBenchProgressAction::RunLevelReset:
		{
			const URunLevelComponent* const RunLevel = URunLevelComponent::FindFor(Shooter);
			State = RunLevel ? FString::Printf(TEXT("level %d"), RunLevel->GetLevel()) : FString(TEXT("no run level"));
			break;
		}
		case EBenchProgressAction::GadgetKindsTwo:
		case EBenchProgressAction::GadgetKindsThree:
		{
			const UInventoryComponent* const Bag = Shooter->GetInventoryComponent();
			State = Bag ? FString::Printf(TEXT("%d kinds, %d in all"), Bag->GetGadgetKinds(), Bag->GetGadgetTotalLimit()) : FString();
			break;
		}
		}
	}

	Label->SetText(FText::FromString(State.IsEmpty() ? GetTitle() : GetTitle() + TEXT("\n") + State));
}

void ABenchProgressButton::HandleButtonPressed(UShootableButtonComponent* Pressed, AActor* Activator)
{
	if (!HasAuthority())
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
	if (!Shooter)
	{
		UE_LOG(LogTemp, Log, TEXT("[BENCH] progress '%s': no player behind %s"), *GetTitle(), *GetNameSafe(Activator));
		return;
	}

	const AShooterWeapon* const Weapon = Shooter->GetCurrentWeapon();
	const UWeaponMasteryDefinition* const Def = Weapon ? Weapon->MasteryDefinition.Get() : nullptr;
	UWeaponMasteryComponent* const Mastery = UWeaponMasteryComponent::FindFor(Shooter);
	URunLevelComponent* const RunLevel = URunLevelComponent::FindFor(Shooter);
	UInventoryComponent* const Bag = Shooter->GetInventoryComponent();
	const int32 GunLevel = (Def && Mastery) ? Mastery->GetLevel(Def) : 0;

	switch (Action)
	{
	case EBenchProgressAction::MasteryUp:    if (Def && Mastery) { Mastery->SetLevelForDebug(Def, GunLevel + 1); } break;
	case EBenchProgressAction::MasteryDown:  if (Def && Mastery) { Mastery->SetLevelForDebug(Def, GunLevel - 1); } break;
	case EBenchProgressAction::MasteryReset: if (Def && Mastery) { Mastery->SetLevelForDebug(Def, 0); } break;
	case EBenchProgressAction::MasteryMax:   if (Def && Mastery) { Mastery->SetLevelForDebug(Def, Def->Steps.Num()); } break;
	case EBenchProgressAction::RunLevelUp:    if (RunLevel) { RunLevel->SetLevelForDebug(RunLevel->GetLevel() + 1); } break;
	case EBenchProgressAction::RunLevelReset: if (RunLevel) { RunLevel->SetLevelForDebug(0); } break;
	case EBenchProgressAction::GadgetKindsTwo:   if (Bag) { Bag->SetGadgetKinds(2); } break;
	case EBenchProgressAction::GadgetKindsThree: if (Bag) { Bag->SetGadgetKinds(3); } break;
	}

	ShowState(Shooter);
	UE_LOG(LogTemp, Log, TEXT("[BENCH] progress '%s' for %s"), *GetTitle(), *Shooter->GetName());
}
