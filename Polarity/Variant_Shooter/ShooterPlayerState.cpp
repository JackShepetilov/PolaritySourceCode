// ShooterPlayerState.cpp

#include "ShooterPlayerState.h"
#include "Variant_Shooter/Buildables/BuildableActor.h"
#include "Variant_Shooter/Buildables/BuildableDefinition.h"
#include "Variant_Shooter/Pickups/MetalPickup.h"
#include "Variant_Shooter/Pickups/LootDropComponent.h"
#include "Coop/CoopPlayers.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"

AShooterPlayerState::AShooterPlayerState()
{
}

void AShooterPlayerState::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		SetMetalInternal(FMath::Clamp(StartingMetal, 0, MaxMetal));
	}
}

void AShooterPlayerState::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// The player is gone (left the session): their buildings go with them, as an engineer's do when
	// he leaves the server. Not on a world teardown, where everything is dying anyway. Walk a copy:
	// each Demolish unregisters.
	const bool bPlayerLeft = EndPlayReason == EEndPlayReason::Destroyed || EndPlayReason == EEndPlayReason::RemovedFromWorld;
	const bool bWorldGoing = GetWorld() && GetWorld()->bIsTearingDown;
	if (HasAuthority() && bPlayerLeft && !bWorldGoing && OwnedBuildables.Num() > 0)
	{
		TArray<TObjectPtr<ABuildableActor>> Standing = OwnedBuildables;
		for (ABuildableActor* Buildable : Standing)
		{
			if (Buildable)
			{
				Buildable->Demolish();
			}
		}
		OwnedBuildables.Reset();
	}

	Super::EndPlay(EndPlayReason);
}

void AShooterPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AShooterPlayerState, Metal);
	DOREPLIFETIME(AShooterPlayerState, MaxMetal);
	DOREPLIFETIME(AShooterPlayerState, OwnedBuildables);
}

// ==================== Buildables ====================

TArray<ABuildableActor*> AShooterPlayerState::GetOwnedBuildables() const
{
	TArray<ABuildableActor*> Out;
	Out.Reserve(OwnedBuildables.Num());
	for (const TObjectPtr<ABuildableActor>& Buildable : OwnedBuildables)
	{
		Out.Add(Buildable.Get());
	}
	return Out;
}

int32 AShooterPlayerState::CountOwnedBuildables(const UBuildableDefinition* Definition) const
{
	int32 Count = 0;
	for (const TObjectPtr<ABuildableActor>& Buildable : OwnedBuildables)
	{
		if (Buildable && !Buildable->IsDestroyed() && (!Definition || Buildable->GetDefinition() == Definition))
		{
			++Count;
		}
	}
	return Count;
}

void AShooterPlayerState::GetOwnedBuildablesOfKind(const UBuildableDefinition* Definition, TArray<ABuildableActor*>& OutBuildables) const
{
	OutBuildables.Reset();
	for (const TObjectPtr<ABuildableActor>& Buildable : OwnedBuildables)
	{
		if (Buildable && !Buildable->IsDestroyed() && (!Definition || Buildable->GetDefinition() == Definition))
		{
			OutBuildables.Add(Buildable);
		}
	}
}

void AShooterPlayerState::RegisterBuildable(ABuildableActor* Buildable)
{
	if (!HasAuthority() || !Buildable || OwnedBuildables.Contains(Buildable))
	{
		return;
	}
	OwnedBuildables.Add(Buildable);
	// The host gets no OnRep for its own write; same announcement, same path.
	OnRep_OwnedBuildables();
}

void AShooterPlayerState::UnregisterBuildable(ABuildableActor* Buildable)
{
	if (!HasAuthority() || !Buildable)
	{
		return;
	}
	if (OwnedBuildables.Remove(Buildable) > 0)
	{
		OnRep_OwnedBuildables();
	}
}

void AShooterPlayerState::OnRep_OwnedBuildables()
{
	OnOwnedBuildablesChanged.Broadcast();
}

int32 AShooterPlayerState::AddMetal(int32 Amount)
{
	if (!HasAuthority() || Amount <= 0)
	{
		return 0;
	}

	const int32 Taken = FMath::Min(Amount, GetMetalRoom());
	if (Taken > 0)
	{
		SetMetalInternal(Metal + Taken);
	}

	UE_LOG(LogTemp, Log, TEXT("[METAL_DEBUG] %s +%d (offered %d) -> %d/%d"),
		*GetPlayerName(), Taken, Amount, Metal, MaxMetal);
	return Taken;
}

bool AShooterPlayerState::TrySpendMetal(int32 Cost)
{
	if (!HasAuthority() || Cost < 0 || Cost > Metal)
	{
		UE_LOG(LogTemp, Log, TEXT("[METAL_DEBUG] %s cannot spend %d (has %d)"), *GetPlayerName(), Cost, Metal);
		return false;
	}

	SetMetalInternal(Metal - Cost);
	UE_LOG(LogTemp, Log, TEXT("[METAL_DEBUG] %s -%d -> %d/%d"), *GetPlayerName(), Cost, Metal, MaxMetal);
	return true;
}

void AShooterPlayerState::SetMetalInternal(int32 NewMetal)
{
	const int32 OldMetal = Metal;
	Metal = FMath::Clamp(NewMetal, 0, MaxMetal);

	// The host never gets an OnRep for its own write, so it announces here. Clients announce from
	// OnRep_Metal. A remote client's copy of the host's PlayerState goes through OnRep as well, which
	// is why this is not a second broadcast for anybody.
	if (Metal != OldMetal)
	{
		OnMetalChanged.Broadcast(Metal, MaxMetal, Metal - OldMetal);
	}
}

void AShooterPlayerState::OnRep_Metal(int32 OldMetal)
{
	OnMetalChanged.Broadcast(Metal, MaxMetal, Metal - OldMetal);
}

void AShooterPlayerState::OnRep_MaxMetal()
{
	// The fields arrive in no fixed order, so the cap redraws the bar on its own as well.
	OnMetalChanged.Broadcast(Metal, MaxMetal, 0);
}

// ==================== Console ====================

namespace MetalDebug
{
	/** The player at THIS screen. A console command is local by nature, which is the one case the
	 *  coop rules allow the local controller for. The write itself still needs the authority. */
	static AShooterPlayerState* FindLocalAuthoritativeState(UWorld* World)
	{
		APlayerController* PC = CoopPlayers::GetLocalController(World);
		AShooterPlayerState* State = PC ? PC->GetPlayerState<AShooterPlayerState>() : nullptr;
		if (!State)
		{
			UE_LOG(LogTemp, Warning, TEXT("[METAL_DEBUG] no AShooterPlayerState on the local controller. Is the GameMode's PlayerStateClass overridden?"));
			return nullptr;
		}
		if (!State->HasAuthority())
		{
			// TODO(COOP): a client typing this would need a server RPC. Not worth one for a cheat.
			UE_LOG(LogTemp, Warning, TEXT("[METAL_DEBUG] metal commands only work on the host or in standalone"));
			return nullptr;
		}
		return State;
	}

	static int32 ParseAmount(const TArray<FString>& Args, int32 Default)
	{
		return Args.Num() > 0 ? FMath::Max(1, FCString::Atoi(*Args[0])) : Default;
	}

	static void CmdAdd(const TArray<FString>& Args, UWorld* World)
	{
		if (AShooterPlayerState* State = FindLocalAuthoritativeState(World))
		{
			State->AddMetal(ParseAmount(Args, 50));
		}
	}

	static void CmdSpend(const TArray<FString>& Args, UWorld* World)
	{
		if (AShooterPlayerState* State = FindLocalAuthoritativeState(World))
		{
			State->TrySpendMetal(ParseAmount(Args, 50));
		}
	}

	static void CmdDrop(const TArray<FString>& Args, UWorld* World)
	{
		AShooterPlayerState* State = FindLocalAuthoritativeState(World);
		APawn* Pawn = State ? State->GetPawn() : nullptr;
		if (!Pawn)
		{
			return;
		}

		// A few metres ahead and a bit up, so the burst is visible and the piles land in reach.
		FLootDropContext Context;
		Context.Location = Pawn->GetActorLocation() + Pawn->GetActorForwardVector() * 400.0f + FVector(0.0f, 0.0f, 150.0f);

		FLootDropEntry Entry;
		Entry.PickupClass = AMetalPickup::StaticClass();
		Entry.Amount = ParseAmount(Args, 20);
		Entry.Count = Args.Num() > 1 ? FMath::Clamp(FCString::Atoi(*Args[1]), 1, 20) : 3;

		ULootDropComponent::SpawnEntry(World, Entry, Context, 150.0f, 30.0f, FVector::ZeroVector, Pawn);
		UE_LOG(LogTemp, Log, TEXT("[METAL_DEBUG] dropped %d piles of %d metal"), Entry.Count, Entry.Amount);
	}
}

static FAutoConsoleCommandWithWorldAndArgs CmdMetalAdd(
	TEXT("polarity.metal.add"),
	TEXT("Give the local player metal. Host or standalone only. Usage: polarity.metal.add [amount=50]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetalDebug::CmdAdd)
);

static FAutoConsoleCommandWithWorldAndArgs CmdMetalSpend(
	TEXT("polarity.metal.spend"),
	TEXT("Spend the local player's metal, all or nothing. Usage: polarity.metal.spend [amount=50]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetalDebug::CmdSpend)
);

static FAutoConsoleCommandWithWorldAndArgs CmdMetalDrop(
	TEXT("polarity.metal.drop"),
	TEXT("Throw metal piles on the floor ahead of the local player. Usage: polarity.metal.drop [metal per pile=20] [piles=3]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetalDebug::CmdDrop)
);
