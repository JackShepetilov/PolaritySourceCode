// FieldCareComponent.cpp

#include "Variant_Shooter/Health/FieldCareComponent.h"

#include "Coop/CoopPlayers.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "Variant_Shooter/Health/StrippableArmor.h"
#include "Variant_Shooter/Inventory/InventoryComponent.h"
#include "Variant_Shooter/ShooterCharacter.h"

UFieldCareComponent::UFieldCareComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UFieldCareComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UFieldCareComponent, Action);
	DOREPLIFETIME(UFieldCareComponent, Duration);
}

bool UFieldCareComponent::IsOwnerLocal() const
{
	const APawn* const Pawn = Cast<APawn>(GetOwner());
	return Pawn && Pawn->IsLocallyControlled();
}

// ==================== Asking ====================

bool UFieldCareComponent::IsBusy() const
{
	const UWorld* const World = GetWorld();
	return Action != EFieldCareAction::None || (World && World->GetTimeSeconds() < PredictedBusyUntil);
}

float UFieldCareComponent::GetProgress() const
{
	const UWorld* const World = GetWorld();
	if (Action == EFieldCareAction::None || Duration <= 0.0f || !World)
	{
		return 0.0f;
	}
	return FMath::Clamp((World->GetTimeSeconds() - LocalStartTime) / Duration, 0.0f, 1.0f);
}

int32 UFieldCareComponent::GetMedkitCount() const
{
	const AShooterCharacter* const Shooter = Cast<AShooterCharacter>(GetOwner());
	const UInventoryComponent* const Inventory = Shooter ? Shooter->GetInventoryComponent() : nullptr;
	return Inventory ? Inventory->GetMedkitCount() : 0;
}

bool UFieldCareComponent::CanUseMedkit() const
{
	const AShooterCharacter* const Shooter = Cast<AShooterCharacter>(GetOwner());
	return Shooter && !Shooter->IsDead() && Shooter->GetCurrentHP() < Shooter->GetMaxHP() - 0.5f && GetMedkitCount() > 0;
}

AStrippableArmor* UFieldCareComponent::FindStrippableArmor() const
{
	const AActor* const Owner = GetOwner();
	UWorld* const World = GetWorld();
	if (!Owner || !World)
	{
		return nullptr;
	}

	// A handful of these lie about at most, and this runs on a key press and for the prompt.
	AStrippableArmor* Best = nullptr;
	float BestDistSq = FMath::Square(StripRange);
	for (TActorIterator<AStrippableArmor> It(World); It; ++It)
	{
		if (It->IsTaken() || It->ArmorAmount <= 0.0f)
		{
			continue;
		}
		const float DistSq = FVector::DistSquared(It->GetActorLocation(), Owner->GetActorLocation());
		if (DistSq <= BestDistSq)
		{
			BestDistSq = DistSq;
			Best = *It;
		}
	}
	return Best;
}

bool UFieldCareComponent::CanStripArmor() const
{
	const AShooterCharacter* const Shooter = Cast<AShooterCharacter>(GetOwner());
	return Shooter && !Shooter->IsDead() && Shooter->GetCurrentArmor() < Shooter->GetMaxArmor() - 0.5f
		&& FindStrippableArmor() != nullptr;
}

// ==================== Doing it ====================

void UFieldCareComponent::BeginMedkit()
{
	Begin(EFieldCareAction::Medkit);
}

void UFieldCareComponent::BeginStripArmor()
{
	Begin(EFieldCareAction::StripArmor);
}

void UFieldCareComponent::Begin(EFieldCareAction Wanted)
{
	if (IsBusy() || (Wanted == EFieldCareAction::Medkit ? !CanUseMedkit() : !CanStripArmor()))
	{
		return;
	}

	// The owner's own hands stop now, not a round trip from now.
	if (IsOwnerLocal())
	{
		if (const UWorld* const World = GetWorld())
		{
			PredictedBusyUntil = World->GetTimeSeconds() + 0.5f;
		}
		FreeHands();
	}

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		StartOnServer(Wanted);
	}
	else
	{
		Server_Begin(Wanted);
	}
}

void UFieldCareComponent::Cancel()
{
	PredictedBusyUntil = -1.0f;

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		if (Action != EFieldCareAction::None)
		{
			GetWorld()->GetTimerManager().ClearTimer(FinishTimer);
			SetAction(EFieldCareAction::None, 0.0f);
		}
	}
	else
	{
		Server_Cancel();
	}
}

void UFieldCareComponent::Server_Begin_Implementation(EFieldCareAction Wanted)
{
	StartOnServer(Wanted);
}

void UFieldCareComponent::Server_Cancel_Implementation()
{
	Cancel();
}

void UFieldCareComponent::StartOnServer(EFieldCareAction Wanted)
{
	UWorld* const World = GetWorld();
	if (!World || Action != EFieldCareAction::None)
	{
		return;
	}

	// Asked again here: the client's word is a request.
	if (Wanted == EFieldCareAction::Medkit)
	{
		if (!CanUseMedkit())
		{
			return;
		}
		SetAction(Wanted, MedkitUseTime);
	}
	else if (Wanted == EFieldCareAction::StripArmor)
	{
		if (!CanStripArmor())
		{
			return;
		}
		StripTarget = FindStrippableArmor();
		SetAction(Wanted, StripTime);
	}
	else
	{
		return;
	}

	World->GetTimerManager().SetTimer(FinishTimer, this, &UFieldCareComponent::FinishOnServer, Duration, false);
}

void UFieldCareComponent::FinishOnServer()
{
	AShooterCharacter* const Shooter = Cast<AShooterCharacter>(GetOwner());
	const EFieldCareAction Done = Action;
	float Amount = 0.0f;

	if (Shooter && !Shooter->IsDead())
	{
		if (Done == EFieldCareAction::Medkit)
		{
			UInventoryComponent* const Inventory = Shooter->GetInventoryComponent();
			if (Inventory && Inventory->ConsumeMedkits(1) == 1)
			{
				const float Before = Shooter->GetCurrentHP();
				Shooter->RestoreHealth(MedkitHealAmount);
				Amount = Shooter->GetCurrentHP() - Before;
			}
		}
		else if (Done == EFieldCareAction::StripArmor)
		{
			AStrippableArmor* const Body = StripTarget.Get();

			// Walked away from it with the key held: nothing.
			if (Body && !Body->IsTaken()
				&& FVector::Dist(Body->GetActorLocation(), Shooter->GetActorLocation()) <= StripRange * 1.5f)
			{
				const float Before = Shooter->GetCurrentArmor();
				Shooter->RestoreArmor(Body->TakeArmor());
				Amount = Shooter->GetCurrentArmor() - Before;
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[CARE] %s finished %s: +%.0f"), *GetNameSafe(GetOwner()),
		Done == EFieldCareAction::Medkit ? TEXT("a medkit") : TEXT("stripping armour"), Amount);

	StripTarget = nullptr;
	SetAction(EFieldCareAction::None, 0.0f);

	OnFinished.Broadcast(Done, Amount);
	if (!IsOwnerLocal())
	{
		Client_Finished(Done, Amount);
	}
}

void UFieldCareComponent::Client_Finished_Implementation(EFieldCareAction Done, float Amount)
{
	OnFinished.Broadcast(Done, Amount);
}

void UFieldCareComponent::SetAction(EFieldCareAction NewAction, float NewDuration)
{
	Action = NewAction;
	Duration = NewDuration;
	OnRep_Action();
}

void UFieldCareComponent::OnRep_Action()
{
	if (const UWorld* const World = GetWorld())
	{
		LocalStartTime = World->GetTimeSeconds();
	}

	// The server has spoken; the prediction has done its job either way.
	PredictedBusyUntil = -1.0f;

	if (Action != EFieldCareAction::None && IsOwnerLocal())
	{
		FreeHands();
	}
	OnActionChanged.Broadcast(Action, Action == EFieldCareAction::None ? 0.0f : Duration);
}

void UFieldCareComponent::FreeHands()
{
	if (AShooterCharacter* const Shooter = Cast<AShooterCharacter>(GetOwner()))
	{
		Shooter->DoStopFiring();
		Shooter->DoStopADS();
	}
}

// ==================== Console ====================
// For trying it before the keys and the HUD exist. A console press is not a hold: it starts the
// action and lets it run to the end.

namespace FieldCareDebug
{
	static void ForEachPlayer(UWorld* World, TFunctionRef<void(AShooterCharacter*, UFieldCareComponent*)> Do)
	{
		if (!World)
		{
			return;
		}
		TArray<APawn*> Pawns;
		CoopPlayers::GetAll(World, Pawns);
		for (APawn* const Pawn : Pawns)
		{
			AShooterCharacter* const Shooter = Cast<AShooterCharacter>(Pawn);
			UFieldCareComponent* const Care = Shooter ? Shooter->FindComponentByClass<UFieldCareComponent>() : nullptr;
			if (Care)
			{
				Do(Shooter, Care);
			}
		}
	}
}

static FAutoConsoleCommandWithWorldAndArgs GCmdCareGive(
	TEXT("polarity.care.give"),
	TEXT("Put medkits in every player's bag. Usage: polarity.care.give [count, default 1]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		const int32 Count = Args.Num() > 0 ? FMath::Max(1, FCString::Atoi(*Args[0])) : 1;
		FieldCareDebug::ForEachPlayer(World, [Count](AShooterCharacter* Shooter, UFieldCareComponent* Care)
		{
			UInventoryComponent* const Inventory = Shooter->GetInventoryComponent();
			if (!Inventory)
			{
				return;
			}
			FInventoryItem Item;
			Item.Kind = EInventorySlotKind::Medkit;
			Item.Count = Count;
			Item.StackMax = Inventory->GetMedkitStackSize();
			const int32 NotTaken = Inventory->TryAdd(Item);
			UE_LOG(LogTemp, Log, TEXT("[CARE] %s: %d medkits added, %d in the bag (ceiling %d)"), *Shooter->GetName(),
				Count - NotTaken, Care->GetMedkitCount(), Inventory->GetMaxMedkits());
		});
	}));

static FAutoConsoleCommandWithWorldAndArgs GCmdCareMedkit(
	TEXT("polarity.care.medkit"),
	TEXT("Every player starts using a medkit and it runs to the end."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		FieldCareDebug::ForEachPlayer(World, [](AShooterCharacter* Shooter, UFieldCareComponent* Care)
		{
			UE_LOG(LogTemp, Log, TEXT("[CARE] %s: medkit asked, can=%d, medkits=%d, hp=%.0f"), *Shooter->GetName(),
				Care->CanUseMedkit() ? 1 : 0, Care->GetMedkitCount(), Shooter->GetCurrentHP());
			Care->BeginMedkit();
		});
	}));

static FAutoConsoleCommandWithWorldAndArgs GCmdCareStrip(
	TEXT("polarity.care.strip"),
	TEXT("Every player starts stripping the armour off the nearest body and it runs to the end."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		FieldCareDebug::ForEachPlayer(World, [](AShooterCharacter* Shooter, UFieldCareComponent* Care)
		{
			UE_LOG(LogTemp, Log, TEXT("[CARE] %s: strip asked, can=%d, body=%s, armour=%.0f"), *Shooter->GetName(),
				Care->CanStripArmor() ? 1 : 0, *GetNameSafe(Care->FindStrippableArmor()), Shooter->GetCurrentArmor());
			Care->BeginStripArmor();
		});
	}));
