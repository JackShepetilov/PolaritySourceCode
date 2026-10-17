// RunLevelComponent.cpp

#include "Variant_Shooter/Progression/RunLevelComponent.h"

#include "Variant_Shooter/Progression/RunLevelDefinition.h"
#include "Coop/CoopPlayers.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"

URunLevelComponent::URunLevelComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void URunLevelComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	// To everyone: two integers, and a teammate's level is what a coop HUD will want.
	DOREPLIFETIME(URunLevelComponent, XP);
	DOREPLIFETIME(URunLevelComponent, Level);
}

URunLevelComponent* URunLevelComponent::FindFor(const AActor* Player)
{
	const APlayerState* PlayerState = Cast<APlayerState>(Player);
	if (!PlayerState)
	{
		if (const APawn* const Pawn = Cast<APawn>(Player))
		{
			PlayerState = Pawn->GetPlayerState();
		}
		else if (const AController* const Controller = Cast<AController>(Player))
		{
			PlayerState = Controller->PlayerState;
		}
	}
	return PlayerState ? PlayerState->FindComponentByClass<URunLevelComponent>() : nullptr;
}

void URunLevelComponent::AddXP(int32 Amount)
{
	AActor* const Owner = GetOwner();
	if (Amount <= 0 || !Owner || !Owner->HasAuthority())
	{
		return;
	}

	XP += Amount;
	const URunLevelDefinition* const Definition = URunLevelSettings::GetDefinition();
	const int32 OldLevel = Level;
	Level = Definition ? Definition->GetLevelForXP(XP) : 0;

	UE_LOG(LogTemp, Log, TEXT("[LEVEL_DEBUG] %s +%d XP = %d, level %d%s (next at %d)"),
		*GetNameSafe(Owner), Amount, XP, Level, Level != OldLevel ? TEXT(" UP") : TEXT(""), GetXPForNextLevel());

	// A player state sends rarely by default: the number on the HUD should not wait for it.
	Owner->ForceNetUpdate();
	OnLevelChanged.Broadcast(Level, XP);
}

void URunLevelComponent::SetLevelForDebug(int32 NewLevel)
{
	AActor* const Owner = GetOwner();
	const URunLevelDefinition* const Definition = URunLevelSettings::GetDefinition();
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}
	Level = Definition ? FMath::Clamp(NewLevel, 0, Definition->Steps.Num()) : 0;
	XP = Level > 0 && Definition ? Definition->GetXPToReach(Level) : 0;
	UE_LOG(LogTemp, Log, TEXT("[LEVEL_DEBUG] %s set to level %d (%d XP)"), *GetNameSafe(Owner), Level, XP);
	Owner->ForceNetUpdate();
	OnLevelChanged.Broadcast(Level, XP);
}

void URunLevelComponent::OnRep_Progress()
{
	OnLevelChanged.Broadcast(Level, XP);
}

int32 URunLevelComponent::GetXPForNextLevel() const
{
	const URunLevelDefinition* const Definition = URunLevelSettings::GetDefinition();
	return Definition ? Definition->GetXPToReach(Level + 1) : -1;
}

int32 URunLevelComponent::GetGadgetsPerKind() const
{
	const URunLevelDefinition* const Definition = URunLevelSettings::GetDefinition();
	return Definition ? Definition->GetGadgetsPerKind(Level) : 0;
}

float URunLevelComponent::GetSuppressionResistance() const
{
	const URunLevelDefinition* const Definition = URunLevelSettings::GetDefinition();
	return Definition ? Definition->GetSuppressionResistance(Level) : 0.0f;
}

// ==================== Debug ====================
// Local by nature, like the inventory's: the player at this screen, on a machine with authority.

namespace RunLevelDebug
{
	static URunLevelComponent* FindLocal(UWorld* World)
	{
		const APlayerController* const PC = CoopPlayers::GetLocalController(World);
		return PC ? URunLevelComponent::FindFor(PC) : nullptr;
	}

	static void CmdXP(const TArray<FString>& Args, UWorld* World)
	{
		URunLevelComponent* const RunLevel = FindLocal(World);
		if (!RunLevel || Args.Num() < 1)
		{
			UE_LOG(LogTemp, Warning, TEXT("[LEVEL_DEBUG] usage: polarity.level.xp <amount> (needs a local player with a run level)"));
			return;
		}
		RunLevel->AddXP(FCString::Atoi(*Args[0]));
	}

	static void CmdShow(UWorld* World)
	{
		const URunLevelComponent* const RunLevel = FindLocal(World);
		if (!RunLevel)
		{
			UE_LOG(LogTemp, Warning, TEXT("[LEVEL_DEBUG] no local player with a run level"));
			return;
		}
		UE_LOG(LogTemp, Log, TEXT("[LEVEL_DEBUG] level %d, %d XP, next at %d; gadgets per kind %d, suppression resistance %.2f; ladder %s"),
			RunLevel->GetLevel(), RunLevel->GetXP(), RunLevel->GetXPForNextLevel(), RunLevel->GetGadgetsPerKind(),
			RunLevel->GetSuppressionResistance(), *GetNameSafe(URunLevelSettings::GetDefinition()));
	}
}

static FAutoConsoleCommandWithWorldAndArgs CmdRunLevelXP(
	TEXT("polarity.level.xp"),
	TEXT("Give the local player run experience. Usage: polarity.level.xp <amount>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunLevelDebug::CmdXP)
);

static FAutoConsoleCommandWithWorld CmdRunLevelShow(
	TEXT("polarity.level.show"),
	TEXT("Print the local player's run level and what it gives."),
	FConsoleCommandWithWorldDelegate::CreateStatic(&RunLevelDebug::CmdShow)
);
