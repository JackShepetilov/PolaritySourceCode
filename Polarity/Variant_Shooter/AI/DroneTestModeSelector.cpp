// DroneTestModeSelector.cpp

#include "DroneTestModeSelector.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "KeepAliveSpawner.h"
#include "ShooterNPC.h"

ADroneTestModeSelector::ADroneTestModeSelector()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
}

void ADroneTestModeSelector::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	// Not BeginPlay: the order actors begin play in is not promised, and a spawner that began first
	// would already have filled up with the level's own settings.
	if (HasAuthority() && Modes.IsValidIndex(ActiveMode))
	{
		Apply(Modes[ActiveMode], false);
	}
	else if (HasAuthority() && Modes.Num() > 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[DRONE_TEST] %s: ActiveMode %d is out of range, spawners keep their own settings"),
			*GetName(), ActiveMode);
	}
}

bool ADroneTestModeSelector::ApplyMode(int32 Index)
{
	if (!Modes.IsValidIndex(Index))
	{
		UE_LOG(LogTemp, Warning, TEXT("[DRONE_TEST] %s: no mode %d\n%s"), *GetName(), Index, *DescribeModes());
		return false;
	}
	if (!HasAuthority())
	{
		// TODO(COOP): a client switching modes would need a server RPC. Not worth one for a debug room.
		UE_LOG(LogTemp, Warning, TEXT("[DRONE_TEST] modes switch on the host or in standalone only"));
		return false;
	}

	ActiveMode = Index;
	Apply(Modes[Index], HasActorBegunPlay());
	return true;
}

bool ADroneTestModeSelector::ApplyModeByName(FName Name)
{
	const int32 Index = Modes.IndexOfByPredicate([Name](const FDroneTestMode& M) { return M.Name == Name; });
	if (Index == INDEX_NONE)
	{
		UE_LOG(LogTemp, Warning, TEXT("[DRONE_TEST] %s: no mode named %s\n%s"), *GetName(), *Name.ToString(), *DescribeModes());
		return false;
	}
	return ApplyMode(Index);
}

FString ADroneTestModeSelector::DescribeModes() const
{
	FString Out;
	for (int32 i = 0; i < Modes.Num(); ++i)
	{
		Out += FString::Printf(TEXT("%s %d %s\n"), i == ActiveMode ? TEXT(">") : TEXT(" "), i, *Modes[i].Name.ToString());
	}
	return Out;
}

void ADroneTestModeSelector::Apply(const FDroneTestMode& Mode, bool bInPlay)
{
	UWorld* const World = GetWorld();
	if (!World)
	{
		return;
	}

	for (TActorIterator<AKeepAliveSpawner> It(World); It; ++It)
	{
		AKeepAliveSpawner* const Spawner = *It;
		const FDroneTestSpawnerSetup* const Setup = Mode.Spawners.FindByPredicate(
			[Spawner](const FDroneTestSpawnerSetup& S) { return S.Spawner == Spawner; });

		if (!OriginalClasses.Contains(Spawner))
		{
			OriginalClasses.Add(Spawner, Spawner->DroneClass);
		}

		if (bInPlay)
		{
			// Lower first, then clear: DespawnAll replaces nothing by itself, but a still-high
			// KeepAlive would refill from the next death callback.
			Spawner->SetKeepAlive(0);
			Spawner->DespawnAll();
		}

		const TSubclassOf<AShooterNPC> Override = Setup ? Setup->DroneClass : nullptr;
		Spawner->DroneClass = Override ? Override : OriginalClasses[Spawner];
		Spawner->SetKeepAlive(Setup ? Setup->KeepAlive : 0);

		UE_LOG(LogTemp, Log, TEXT("[DRONE_TEST] mode %s: %s keeps %d of %s"), *Mode.Name.ToString(),
			*Spawner->GetName(), Spawner->KeepAlive, *GetNameSafe(Spawner->DroneClass));
	}
}

// ---------------------------------------------------------------------------------------------
// Console

namespace DroneTestDebug
{
	static ADroneTestModeSelector* FindSelector(UWorld* World)
	{
		for (TActorIterator<ADroneTestModeSelector> It(World); It; ++It)
		{
			return *It;
		}
		UE_LOG(LogTemp, Warning, TEXT("[DRONE_TEST] no ADroneTestModeSelector in this level"));
		return nullptr;
	}

	static void CmdMode(const TArray<FString>& Args, UWorld* World)
	{
		ADroneTestModeSelector* const Selector = FindSelector(World);
		if (!Selector)
		{
			return;
		}
		if (Args.Num() == 0)
		{
			UE_LOG(LogTemp, Log, TEXT("[DRONE_TEST] modes:\n%s"), *Selector->DescribeModes());
			return;
		}
		if (Args[0].IsNumeric())
		{
			Selector->ApplyMode(FCString::Atoi(*Args[0]));
		}
		else
		{
			Selector->ApplyModeByName(FName(*Args[0]));
		}
	}
}

static FAutoConsoleCommandWithWorldAndArgs CmdDroneTestMode(
	TEXT("polarity.dronetest.mode"),
	TEXT("Switch the test room's spawner mode. No argument lists them. Usage: polarity.dronetest.mode [name|index]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&DroneTestDebug::CmdMode)
);
