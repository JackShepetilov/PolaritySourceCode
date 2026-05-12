// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "ArenaAntenna.h"
#include "Polarity/Variant_Shooter/ShootableButton.h"
#include "Polarity/Variant_Shooter/ShootableButtonComponent.h"
#include "Polarity/Subtitle/SubtitleSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "NiagaraFunctionLibrary.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "EngineUtils.h"

AArenaAntenna::AArenaAntenna()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	AntennaMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("AntennaMesh"));
	AntennaMesh->SetupAttachment(SceneRoot);
	AntennaMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);

	BeaconVFX = CreateDefaultSubobject<UNiagaraComponent>(TEXT("BeaconVFX"));
	BeaconVFX->SetupAttachment(SceneRoot);
	BeaconVFX->bAutoActivate = false; // Off until ArenaManager flips us to AvailablePostFight
}

void AArenaAntenna::BeginPlay()
{
	Super::BeginPlay();

	// Two interaction modes supported:
	//   (a) UShootableButtonComponent added INSIDE this antenna's Blueprint via "Add Component"
	//       — single self-contained actor, designer puts the button mesh directly on the antenna.
	//   (b) InteractionButton soft pointer to a separate AShootableButton actor placed nearby.
	// Embedded component wins if both are configured.
	UShootableButtonComponent* EmbeddedButton = FindComponentByClass<UShootableButtonComponent>();
	if (EmbeddedButton)
	{
		EmbeddedButton->OnButtonPressed.AddDynamic(this, &AArenaAntenna::HandleButtonPressed);
		bBoundToButton = true;
		UE_LOG(LogTemp, Warning, TEXT("[ANTENNA_DEBUG] [%s] BeginPlay: Bound to EMBEDDED ShootableButtonComponent (%s)"),
			*GetName(), *EmbeddedButton->GetName());
	}
	else if (AShootableButton* Button = InteractionButton.LoadSynchronous())
	{
		if (Button->ButtonComponent)
		{
			Button->ButtonComponent->OnButtonPressed.AddDynamic(this, &AArenaAntenna::HandleButtonPressed);
			bBoundToButton = true;
			UE_LOG(LogTemp, Warning, TEXT("[ANTENNA_DEBUG] [%s] BeginPlay: Bound to EXTERNAL button actor %s"),
				*GetName(), *Button->GetName());
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[ANTENNA_DEBUG] [%s] BeginPlay: InteractionButton actor %s has NULL ButtonComponent"),
				*GetName(), *Button->GetName());
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[ANTENNA_DEBUG] [%s] BeginPlay: NO interaction source found — neither embedded ShootableButtonComponent nor external InteractionButton actor. Antenna only responds to BP-driven TryActivate()"),
			*GetName());
	}

	UE_LOG(LogTemp, Warning, TEXT("[ANTENNA_DEBUG] [%s] BeginPlay: Initial state=%d, DialogueChoices=%d"),
		*GetName(), (int32)State, DialogueChoices.Num());

	// Sync visuals to whatever the initial state is (default Inactive — beacon off)
	ApplyStateVisuals(State);
}

void AArenaAntenna::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bBoundToButton)
	{
		// Try both possible sources — RemoveDynamic is a no-op if we weren't bound there
		if (UShootableButtonComponent* EmbeddedButton = FindComponentByClass<UShootableButtonComponent>())
		{
			EmbeddedButton->OnButtonPressed.RemoveDynamic(this, &AArenaAntenna::HandleButtonPressed);
		}
		if (AShootableButton* Button = InteractionButton.Get())
		{
			if (Button->ButtonComponent)
			{
				Button->ButtonComponent->OnButtonPressed.RemoveDynamic(this, &AArenaAntenna::HandleButtonPressed);
			}
		}
		bBoundToButton = false;
	}

	Super::EndPlay(EndPlayReason);
}

void AArenaAntenna::SetState(EAntennaState NewState)
{
	if (State == NewState)
	{
		return;
	}

	const EAntennaState OldState = State;
	State = NewState;

	UE_LOG(LogTemp, Warning, TEXT("[ANTENNA_DEBUG] [%s] SetState: %d -> %d"),
		*GetName(), (int32)OldState, (int32)NewState);

	ApplyStateVisuals(NewState);
	OnStateChanged.Broadcast(this, NewState);
}

void AArenaAntenna::TryActivate()
{
	UE_LOG(LogTemp, Warning, TEXT("[ANTENNA_DEBUG] [%s] TryActivate called — current state=%d (0=Inactive, 1=MidFight, 2=PostFight, 3=Activated)"),
		*GetName(), (int32)State);

	const bool bCanActivate =
		State == EAntennaState::AvailableMidFight ||
		State == EAntennaState::AvailablePostFight;

	if (!bCanActivate)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ANTENNA_DEBUG] [%s] TryActivate REJECTED — state=%d (need Available*). Did the player enter the EntryTrigger so ArenaManager could flip antennas to MidFight?"),
			*GetName(), (int32)State);
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[ANTENNA_DEBUG] [%s] ACTIVATED (was state=%d)"), *GetName(), (int32)State);

	// Run dialogue resolution BEFORE flipping to Activated so this antenna still counts
	// out the "other" antenna search the same way for designers reasoning about distances.
	PlayContextualDialogue();

	SetState(EAntennaState::Activated);
	OnActivated.Broadcast(this);
}

void AArenaAntenna::HandleButtonPressed(UShootableButtonComponent* Button, AActor* Activator)
{
	UE_LOG(LogTemp, Warning, TEXT("[ANTENNA_DEBUG] [%s] HandleButtonPressed fired (button=%s, activator=%s) — routing to TryActivate"),
		*GetName(),
		Button ? *Button->GetName() : TEXT("NULL"),
		Activator ? *Activator->GetName() : TEXT("NULL"));
	TryActivate();
}

void AArenaAntenna::ApplyStateVisuals(EAntennaState NewState)
{
	if (!BeaconVFX)
	{
		UE_LOG(LogTemp, Error, TEXT("[ANTENNA_DEBUG] [%s] ApplyStateVisuals: BeaconVFX component is NULL"), *GetName());
		return;
	}

	const bool bShouldBeacon = (NewState == EAntennaState::AvailablePostFight);

	UE_LOG(LogTemp, Warning, TEXT("[ANTENNA_DEBUG] [%s] ApplyStateVisuals: state=%d, bShouldBeacon=%d, BeaconVFXAsset=%s"),
		*GetName(), (int32)NewState, bShouldBeacon ? 1 : 0,
		BeaconVFXAsset ? *BeaconVFXAsset->GetName() : TEXT("NULL"));

	if (bShouldBeacon && BeaconVFXAsset)
	{
		// (Re)assign the asset in case it changed and start the system fresh
		BeaconVFX->SetAsset(BeaconVFXAsset);
		BeaconVFX->Activate(true);
		UE_LOG(LogTemp, Warning, TEXT("[ANTENNA_DEBUG] [%s] ApplyStateVisuals: Beacon ACTIVATED with asset %s"),
			*GetName(), *BeaconVFXAsset->GetName());
	}
	else
	{
		BeaconVFX->Deactivate();
		if (bShouldBeacon && !BeaconVFXAsset)
		{
			UE_LOG(LogTemp, Error, TEXT("[ANTENNA_DEBUG] [%s] ApplyStateVisuals: WANT beacon but BeaconVFXAsset is NULL — set it in BP_Antenna Details"),
				*GetName());
		}
	}
}

// ==================== Dialogue ====================

void AArenaAntenna::PlayContextualDialogue()
{
	if (DialogueChoices.Num() == 0)
	{
		return;
	}

	const float Distance = GetDistanceToNearestOtherAntenna();
	const FAntennaDialogueChoice* Choice = PickDialogueChoiceForDistance(Distance);
	if (!Choice || !Choice->SubtitleTable || Choice->SubtitlePrefix.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("ArenaAntenna [%s]: No usable dialogue choice (distance=%.0f)"),
			*GetName(), Distance);
		return;
	}

	UGameInstance* GI = GetGameInstance();
	USubtitleSubsystem* Subs = GI ? GI->GetSubsystem<USubtitleSubsystem>() : nullptr;
	if (!Subs)
	{
		UE_LOG(LogTemp, Warning, TEXT("ArenaAntenna [%s]: SubtitleSubsystem unavailable, skipping dialogue"),
			*GetName());
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("ArenaAntenna [%s]: Playing dialogue prefix=%s (distance=%.0f, range=[%.0f..%.0f])"),
		*GetName(), *Choice->SubtitlePrefix, Distance, Choice->MinDistance, Choice->MaxDistance);

	Subs->ShowSubtitleSequence(Choice->SubtitleTable, Choice->SubtitlePrefix);
}

float AArenaAntenna::GetDistanceToNearestOtherAntenna() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return -1.0f;
	}

	const FVector MyLocation = GetActorLocation();
	float NearestSquared = -1.0f;

	for (TActorIterator<AArenaAntenna> It(World); It; ++It)
	{
		AArenaAntenna* Other = *It;
		if (!Other || Other == this)
		{
			continue;
		}
		if (Other->State == EAntennaState::Activated)
		{
			continue;
		}

		const float DistSq = FVector::DistSquared(MyLocation, Other->GetActorLocation());
		if (NearestSquared < 0.0f || DistSq < NearestSquared)
		{
			NearestSquared = DistSq;
		}
	}

	return (NearestSquared < 0.0f) ? -1.0f : FMath::Sqrt(NearestSquared);
}

const FAntennaDialogueChoice* AArenaAntenna::PickDialogueChoiceForDistance(float Distance) const
{
	if (DialogueChoices.Num() == 0)
	{
		return nullptr;
	}

	// Negative distance means "no other antenna found" — fall back to the first entry
	if (Distance < 0.0f)
	{
		return &DialogueChoices[0];
	}

	for (const FAntennaDialogueChoice& Choice : DialogueChoices)
	{
		if (Distance >= Choice.MinDistance && Distance < Choice.MaxDistance)
		{
			return &Choice;
		}
	}

	// No band matched — fall back to first entry so dialogue still plays (designer can fix table)
	return &DialogueChoices[0];
}
