// VFXPrewarmSubsystem.cpp

#include "VFXPrewarmSubsystem.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "Engine/StreamableManager.h"
#include "Engine/AssetManager.h"

void UVFXPrewarmSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	SystemsToPrewarm.Empty();
	PrewarmComponents.Empty();
	bPrewarmComplete = false;
	bPrewarmStarted = false;
	PrewarmedCount = 0;
}

void UVFXPrewarmSubsystem::Deinitialize()
{
	// Clear timer
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PrewarmTimerHandle);
	}

	// Destroy any remaining prewarm components
	for (UNiagaraComponent* Comp : PrewarmComponents)
	{
		if (IsValid(Comp))
		{
			Comp->DestroyComponent();
		}
	}
	PrewarmComponents.Empty();
	SystemsToPrewarm.Empty();
	PrewarmList = nullptr;

	Super::Deinitialize();
}

bool UVFXPrewarmSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// Only create in game worlds
	if (UWorld* World = Cast<UWorld>(Outer))
	{
		return World->IsGameWorld();
	}
	return false;
}

void UVFXPrewarmSubsystem::SetPrewarmList(UVFXPrewarmList* InPrewarmList)
{
	PrewarmList = InPrewarmList;
	LoadPrewarmList();
}

void UVFXPrewarmSubsystem::LoadPrewarmList()
{
	if (!PrewarmList)
	{
		return;
	}

	for (const TSoftObjectPtr<UNiagaraSystem>& SoftSystem : PrewarmList->SystemsToPrewarm)
	{
		if (UNiagaraSystem* System = SoftSystem.LoadSynchronous())
		{
			SystemsToPrewarm.Add(System);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("VFXPrewarmSubsystem: Loaded %d systems from prewarm list"), SystemsToPrewarm.Num());
}

void UVFXPrewarmSubsystem::RegisterSystemForPrewarm(UNiagaraSystem* System)
{
	if (!System)
	{
		return;
	}

	// If prewarm already started, spawn immediately
	if (bPrewarmStarted)
	{
		if (!SystemsToPrewarm.Contains(System))
		{
			SystemsToPrewarm.Add(System);
			PrewarmSystem(System);
		}
	}
	else
	{
		SystemsToPrewarm.Add(System);
	}
}

void UVFXPrewarmSubsystem::RegisterSystemsForPrewarm(const TArray<UNiagaraSystem*>& Systems)
{
	for (UNiagaraSystem* System : Systems)
	{
		RegisterSystemForPrewarm(System);
	}
}

void UVFXPrewarmSubsystem::PrewarmAllSystems()
{
	if (bPrewarmStarted)
	{
		return;
	}

	bPrewarmStarted = true;

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("VFXPrewarmSubsystem: Starting prewarm of %d Niagara systems"), SystemsToPrewarm.Num());

	// Spawn each registered system
	for (UNiagaraSystem* System : SystemsToPrewarm)
	{
		PrewarmSystem(System);
	}

	// Schedule cleanup
	World->GetTimerManager().SetTimer(
		PrewarmTimerHandle,
		this,
		&UVFXPrewarmSubsystem::OnPrewarmComplete,
		PrewarmDuration,
		false
	);
}

void UVFXPrewarmSubsystem::PrewarmSystem(UNiagaraSystem* System)
{
	if (!System)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Spawn the effect far below the level where it's invisible
	// This forces shader compilation for this Niagara system
	UNiagaraComponent* Comp = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		World,
		System,
		PrewarmLocation,
		FRotator::ZeroRotator,
		FVector::OneVector,
		false,  // bAutoDestroy - we'll destroy manually
		true,   // bAutoActivate
		ENCPoolMethod::None,  // Don't use pooling for prewarm
		true    // bPreCullCheck - skip culling
	);

	if (Comp)
	{
		PrewarmComponents.Add(Comp);
		PrewarmedCount++;
		UE_LOG(LogTemp, Verbose, TEXT("VFXPrewarmSubsystem: Prewarmed %s"), *System->GetName());
	}
}

void UVFXPrewarmSubsystem::OnPrewarmComplete()
{
	UE_LOG(LogTemp, Log, TEXT("VFXPrewarmSubsystem: Prewarm complete, cleaning up %d components"), PrewarmComponents.Num());

	// Destroy all prewarm components
	for (UNiagaraComponent* Comp : PrewarmComponents)
	{
		if (IsValid(Comp))
		{
			Comp->DeactivateImmediate();
			Comp->DestroyComponent();
		}
	}
	PrewarmComponents.Empty();

	bPrewarmComplete = true;
}
