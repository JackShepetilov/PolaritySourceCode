// TutorialTriggerVolume.cpp
// Trigger volume implementation

#include "TutorialTriggerVolume.h"
#include "TutorialSubsystem.h"
#include "Components/BoxComponent.h"
#include "Components/BillboardComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Polarity.h"

ATutorialTriggerVolume::ATutorialTriggerVolume()
{
	PrimaryActorTick.bCanEverTick = false;

	// Create trigger box
	TriggerBox = CreateDefaultSubobject<UBoxComponent>(TEXT("TriggerBox"));
	TriggerBox->SetBoxExtent(FVector(200.0f, 200.0f, 100.0f));
	TriggerBox->SetCollisionProfileName(TEXT("Trigger"));
	TriggerBox->SetGenerateOverlapEvents(true);
	RootComponent = TriggerBox;

#if WITH_EDITORONLY_DATA
	// Create editor billboard
	EditorBillboard = CreateDefaultSubobject<UBillboardComponent>(TEXT("EditorBillboard"));
	EditorBillboard->SetupAttachment(RootComponent);
	EditorBillboard->SetHiddenInGame(true);
#endif
}

void ATutorialTriggerVolume::BeginPlay()
{
	Super::BeginPlay();

	// Bind overlap events
	TriggerBox->OnComponentBeginOverlap.AddDynamic(this, &ATutorialTriggerVolume::OnTriggerBeginOverlap);
	TriggerBox->OnComponentEndOverlap.AddDynamic(this, &ATutorialTriggerVolume::OnTriggerEndOverlap);
}

void ATutorialTriggerVolume::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnbindInputCompletion();

	Super::EndPlay(EndPlayReason);
}

// ==================== Overlap Handlers ====================

void ATutorialTriggerVolume::OnTriggerBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (!PassesFilter(OtherActor))
	{
		return;
	}

	// Check if already triggered and set to trigger once
	if (bTriggerOnce && bHasTriggered)
	{
		return;
	}

	UTutorialSubsystem* Subsystem = GetTutorialSubsystem();
	if (!Subsystem)
	{
		UE_LOG(LogPolarity, Warning, TEXT("TutorialTriggerVolume: Cannot get TutorialSubsystem"));
		return;
	}

	// Check if already completed
	if (Subsystem->IsCompleted(TutorialID))
	{
		return;
	}

	bPlayerInside = true;

	// Get player controller
	APlayerController* PC = nullptr;
	if (APawn* Pawn = Cast<APawn>(OtherActor))
	{
		PC = Cast<APlayerController>(Pawn->GetController());
	}

	bool bShown = false;

	// Show appropriate tutorial type
	switch (TutorialType)
	{
	case ETutorialType::Hint:
		bShown = Subsystem->ShowHint(TutorialID, HintData, PC);
		if (bShown && HintData.CompletionType == ETutorialCompletionType::OnInputAction)
		{
			BindInputCompletion();
		}
		break;

	case ETutorialType::Slide:
		bShown = Subsystem->ShowSlide(TutorialID, SlideData, PC);
		break;
	}

	if (bShown)
	{
		bHasTriggered = true;
		UE_LOG(LogPolarity, Log, TEXT("Tutorial triggered: %s"), *TutorialID.ToString());
	}
}

void ATutorialTriggerVolume::OnTriggerEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (!PassesFilter(OtherActor))
	{
		return;
	}

	bPlayerInside = false;

	// Handle exit-based completion for hints
	if (TutorialType == ETutorialType::Hint && bHideOnExit)
	{
		if (UTutorialSubsystem* Subsystem = GetTutorialSubsystem())
		{
			if (Subsystem->IsHintActive())
			{
				bool bMarkCompleted = (HintData.CompletionType == ETutorialCompletionType::OnExitVolume);
				Subsystem->HideHint(bMarkCompleted);
				UnbindInputCompletion();
			}
		}
	}
}

// ==================== Internal ====================

bool ATutorialTriggerVolume::PassesFilter(AActor* Actor) const
{
	if (!Actor)
	{
		return false;
	}

	// Must be a pawn
	APawn* Pawn = Cast<APawn>(Actor);
	if (!Pawn)
	{
		return false;
	}

	// Check tag filter if specified
	if (!RequiredActorTag.IsNone())
	{
		if (!Actor->ActorHasTag(RequiredActorTag))
		{
			return false;
		}
	}

	return true;
}

UTutorialSubsystem* ATutorialTriggerVolume::GetTutorialSubsystem() const
{
	if (UWorld* World = GetWorld())
	{
		if (UGameInstance* GI = World->GetGameInstance())
		{
			return GI->GetSubsystem<UTutorialSubsystem>();
		}
	}
	return nullptr;
}

void ATutorialTriggerVolume::BindInputCompletion()
{
	// Use primary input action for completion
	UInputAction* PrimaryAction = HintData.GetPrimaryInputAction();
	if (!PrimaryAction)
	{
		UE_LOG(LogPolarity, Log, TEXT("No input action for completion binding on tutorial: %s"), *TutorialID.ToString());
		return;
	}

	// Get player controller
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC)
	{
		return;
	}

	APawn* Pawn = PC->GetPawn();
	if (!Pawn)
	{
		return;
	}

	// Get enhanced input component
	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(Pawn->InputComponent);
	if (!EnhancedInput)
	{
		UE_LOG(LogPolarity, Warning, TEXT("BindInputCompletion: No EnhancedInputComponent found for tutorial: %s"), *TutorialID.ToString());
		return;
	}

	// Bind to the primary input action - use Started for single press detection
	EnhancedInput->BindAction(PrimaryAction, ETriggerEvent::Started, this, &ATutorialTriggerVolume::OnInputActionTriggered);

	UE_LOG(LogPolarity, Log, TEXT("Bound input completion for tutorial: %s"), *TutorialID.ToString());
}

void ATutorialTriggerVolume::UnbindInputCompletion()
{
	// Input bindings are automatically cleaned up when the component is destroyed
	// or when the pawn is destroyed, so we don't need explicit cleanup here
}

void ATutorialTriggerVolume::OnInputActionTriggered()
{
	UTutorialSubsystem* Subsystem = GetTutorialSubsystem();
	if (!Subsystem)
	{
		return;
	}

	// Complete if hint is still active (removed bPlayerInside check - unreliable)
	if (Subsystem->IsHintActive())
	{
		Subsystem->HideHint(true);
		UE_LOG(LogPolarity, Log, TEXT("Tutorial completed via input: %s"), *TutorialID.ToString());
	}
}