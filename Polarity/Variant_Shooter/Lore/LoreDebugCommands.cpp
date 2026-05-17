// LoreDebugCommands.cpp
// Console commands for debugging the lore progression system. All prefixed with `lore.`
// Type `lore.help` in the console for the list.

#include "LoreSubsystem.h"
#include "LoreTypes.h"
#include "ChatBroker.h"
#include "StreamSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

namespace LoreDebug
{
	static ULoreSubsystem* FindLore(UWorld* World)
	{
		if (!World) { return nullptr; }
		UGameInstance* GI = World->GetGameInstance();
		if (!GI)     { return nullptr; }
		return GI->GetSubsystem<ULoreSubsystem>();
	}

	static UChatBroker* FindBroker(UWorld* World)
	{
		if (!World) { return nullptr; }
		UGameInstance* GI = World->GetGameInstance();
		if (!GI)     { return nullptr; }
		UStreamSubsystem* Sub = GI->GetSubsystem<UStreamSubsystem>();
		return Sub ? Sub->GetChatBroker() : nullptr;
	}

	static void CmdList(const TArray<FString>& Args, UWorld* World)
	{
		ULoreSubsystem* Lore = FindLore(World);
		if (!Lore)
		{
			UE_LOG(LogTemp, Warning, TEXT("[LORE_DEBUG] LoreSubsystem not available"));
			return;
		}

		const TArray<FName> All = Lore->GetAllLoreIDs();
		UE_LOG(LogTemp, Log, TEXT("[LORE_DEBUG] %d total lore entries; %d consumed:"),
			All.Num(), Lore->GetConsumedCount());
		for (const FName& ID : All)
		{
			const bool bConsumed = Lore->IsLoreConsumed(ID);
			const bool bAvail = Lore->IsLoreAvailable(ID);
			UE_LOG(LogTemp, Log, TEXT("  %s  [%s%s]"),
				*ID.ToString(),
				bConsumed ? TEXT("CONSUMED") : (bAvail ? TEXT("available") : TEXT("locked")),
				TEXT(""));
		}
	}

	static void CmdConsume(const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogTemp, Warning, TEXT("[LORE_DEBUG] Usage: lore.consume <LoreID>"));
			return;
		}
		ULoreSubsystem* Lore = FindLore(World);
		if (!Lore) { return; }
		const FName ID = FName(*Args[0]);
		Lore->MarkLoreConsumed(ID);
		UE_LOG(LogTemp, Log, TEXT("[LORE_DEBUG] Marked %s consumed"), *ID.ToString());
	}

	static void CmdReset(const TArray<FString>& Args, UWorld* World)
	{
		ULoreSubsystem* Lore = FindLore(World);
		if (!Lore) { return; }
		Lore->DebugReset();
	}

	static void CmdTrigger(const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogTemp, Warning, TEXT("[LORE_DEBUG] Usage: lore.trigger <ArenaTag> [Biome]"));
			return;
		}
		ULoreSubsystem* Lore = FindLore(World);
		if (!Lore) { return; }
		UChatBroker* Broker = FindBroker(World);

		const FName ArenaTag = FName(*Args[0]);
		const FName Biome    = (Args.Num() >= 2) ? FName(*Args[1]) : NAME_None;

		FLoreEntryRow Entry;
		if (Lore->PickAndConsumeLoreForArena(ArenaTag, Biome, Entry))
		{
			UE_LOG(LogTemp, Log, TEXT("[LORE_DEBUG] Picked %s (scripted=%s)"),
				*Entry.LoreID.ToString(), *Entry.ChatScriptedSequenceID.ToString());
			if (Broker && !Entry.ChatScriptedSequenceID.IsNone())
			{
				Broker->RunScripted(Entry.ChatScriptedSequenceID);
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[LORE_DEBUG] No eligible lore for arena=%s biome=%s"),
				*ArenaTag.ToString(), *Biome.ToString());
		}
	}

	static void CmdUnconsumed(const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogTemp, Warning, TEXT("[LORE_DEBUG] Usage: lore.unconsumed <Biome>"));
			return;
		}
		ULoreSubsystem* Lore = FindLore(World);
		if (!Lore) { return; }
		const TArray<FName> Items = Lore->GetUnconsumedInBiome(FName(*Args[0]));
		UE_LOG(LogTemp, Log, TEXT("[LORE_DEBUG] Biome %s has %d unconsumed:"), *Args[0], Items.Num());
		for (const FName& ID : Items)
		{
			UE_LOG(LogTemp, Log, TEXT("  %s"), *ID.ToString());
		}
	}

	static void CmdHelp(const TArray<FString>& Args, UWorld* World)
	{
		UE_LOG(LogTemp, Log, TEXT("==================== Lore Debug Commands ===================="));
		UE_LOG(LogTemp, Log, TEXT("  lore.list                          List all lore entries with status"));
		UE_LOG(LogTemp, Log, TEXT("  lore.unconsumed <Biome>            Show unconsumed in biome"));
		UE_LOG(LogTemp, Log, TEXT("  lore.consume <LoreID>              Force-mark a lore entry consumed"));
		UE_LOG(LogTemp, Log, TEXT("  lore.reset                         Clear all consumed (start over)"));
		UE_LOG(LogTemp, Log, TEXT("  lore.trigger <ArenaTag> [Biome]    Simulate antenna activation:"));
		UE_LOG(LogTemp, Log, TEXT("                                       pick lore + run chat sequence"));
		UE_LOG(LogTemp, Log, TEXT("  lore.help                          This help"));
		UE_LOG(LogTemp, Log, TEXT("============================================================="));
	}
}

// ==================== Registration ====================

static FAutoConsoleCommandWithWorldAndArgs CmdLoreList(
	TEXT("lore.list"),
	TEXT("List all lore entries with status."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&LoreDebug::CmdList)
);

static FAutoConsoleCommandWithWorldAndArgs CmdLoreUnconsumed(
	TEXT("lore.unconsumed"),
	TEXT("List unconsumed lore in biome. Usage: lore.unconsumed <Biome>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&LoreDebug::CmdUnconsumed)
);

static FAutoConsoleCommandWithWorldAndArgs CmdLoreConsume(
	TEXT("lore.consume"),
	TEXT("Force-mark a lore entry consumed. Usage: lore.consume <LoreID>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&LoreDebug::CmdConsume)
);

static FAutoConsoleCommandWithWorldAndArgs CmdLoreReset(
	TEXT("lore.reset"),
	TEXT("Clear all consumed lore (start over)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&LoreDebug::CmdReset)
);

static FAutoConsoleCommandWithWorldAndArgs CmdLoreTrigger(
	TEXT("lore.trigger"),
	TEXT("Simulate antenna lore activation. Usage: lore.trigger <ArenaTag> [Biome]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&LoreDebug::CmdTrigger)
);

static FAutoConsoleCommandWithWorldAndArgs CmdLoreHelp(
	TEXT("lore.help"),
	TEXT("List all lore debug commands."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&LoreDebug::CmdHelp)
);
