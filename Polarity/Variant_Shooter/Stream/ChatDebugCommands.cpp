// ChatDebugCommands.cpp
// Console commands for debugging the chat broker. All commands prefixed with `stream.`
// Type `stream.help` in the in-editor console for the list.
//
// No header file — registration via FAutoConsoleCommandWithWorldAndArgs statics.
// Live Coding note: first-time registration of new static initializers may require
// a regular Build (not a full rebuild). After that, command logic edits work with
// Live Coding alone.

#include "ChatBroker.h"
#include "ChatTypes.h"
#include "StreamSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameplayTagContainer.h"
#include "HAL/IConsoleManager.h"

namespace ChatDebug
{
	static UChatBroker* FindBroker(UWorld* World)
	{
		if (!World) { return nullptr; }
		UGameInstance* GI = World->GetGameInstance();
		if (!GI)     { return nullptr; }
		UStreamSubsystem* Sub = GI->GetSubsystem<UStreamSubsystem>();
		if (!Sub)    { return nullptr; }
		return Sub->GetChatBroker();
	}

	/** Normalizes "Headshot" → "Chat.Event.Headshot". Accepts full tag too. */
	static FGameplayTag NormalizeReactionTag(const FString& Input)
	{
		FString Name = Input;
		if (!Name.StartsWith(TEXT("Chat.Event.")))
		{
			Name = FString::Printf(TEXT("Chat.Event.%s"), *Input);
		}
		return FGameplayTag::RequestGameplayTag(FName(*Name), /*ErrorIfNotFound*/ false);
	}

	// ==================== Commands ====================

	static void CmdReaction(const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogTemp, Warning, TEXT("[STREAM_DEBUG] Usage: stream.reaction <TagName>   e.g. stream.reaction Headshot"));
			return;
		}
		UChatBroker* Broker = FindBroker(World);
		if (!Broker)
		{
			UE_LOG(LogTemp, Warning, TEXT("[STREAM_DEBUG] ChatBroker not available (subsystem not initialized or run not started?)"));
			return;
		}

		const FGameplayTag Tag = NormalizeReactionTag(Args[0]);
		if (!Tag.IsValid())
		{
			UE_LOG(LogTemp, Warning, TEXT("[STREAM_DEBUG] Tag '%s' is not registered. Check Project Settings → GameplayTags."), *Args[0]);
			return;
		}

		Broker->EmitReaction(Tag);
		UE_LOG(LogTemp, Log, TEXT("[STREAM_DEBUG] Fired reaction %s"), *Tag.ToString());
	}

	static void CmdScripted(const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogTemp, Warning, TEXT("[STREAM_DEBUG] Usage: stream.scripted <SequenceID>   e.g. stream.scripted regulars_argue"));
			return;
		}
		UChatBroker* Broker = FindBroker(World);
		if (!Broker) { return; }

		Broker->RunScripted(FName(*Args[0]));
		UE_LOG(LogTemp, Log, TEXT("[STREAM_DEBUG] Started scripted sequence '%s'"), *Args[0]);
	}

	static void CmdStopScripted(const TArray<FString>& Args, UWorld* World)
	{
		UChatBroker* Broker = FindBroker(World);
		if (!Broker) { return; }
		Broker->StopAllScripted();
		UE_LOG(LogTemp, Log, TEXT("[STREAM_DEBUG] Stopped all scripted sequences"));
	}

	static void CmdBurst(const TArray<FString>& Args, UWorld* World)
	{
		UChatBroker* Broker = FindBroker(World);
		if (!Broker) { return; }

		int32 Count = 6;
		if (Args.Num() >= 1) { Count = FCString::Atoi(*Args[0]); }
		Count = FMath::Clamp(Count, 1, 50);

		const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Chat.Event.HypeBurst")), false);
		if (!Tag.IsValid())
		{
			UE_LOG(LogTemp, Warning, TEXT("[STREAM_DEBUG] Chat.Event.HypeBurst tag not registered."));
			return;
		}

		for (int32 i = 0; i < Count; ++i)
		{
			Broker->EmitReaction(Tag);
		}
		UE_LOG(LogTemp, Log, TEXT("[STREAM_DEBUG] Fired hype burst (%d emit calls)"), Count);
	}

	static void CmdQuickTag(const FString& TagShortName, UWorld* World)
	{
		UChatBroker* Broker = FindBroker(World);
		if (!Broker) { return; }
		const FGameplayTag Tag = NormalizeReactionTag(TagShortName);
		if (!Tag.IsValid())
		{
			UE_LOG(LogTemp, Warning, TEXT("[STREAM_DEBUG] Tag Chat.Event.%s not registered"), *TagShortName);
			return;
		}
		Broker->EmitReaction(Tag);
	}

	static void CmdSay(const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 2)
		{
			UE_LOG(LogTemp, Warning, TEXT("[STREAM_DEBUG] Usage: stream.say <Username> <Message words...>"));
			return;
		}
		UChatBroker* Broker = FindBroker(World);
		if (!Broker) { return; }

		const FString Username = Args[0];
		FString Message;
		for (int32 i = 1; i < Args.Num(); ++i)
		{
			if (i > 1) { Message += TEXT(" "); }
			Message += Args[i];
		}

		Broker->DebugEmit(Username, FText::FromString(Message), EChatMessageKind::Debug);
		UE_LOG(LogTemp, Log, TEXT("[STREAM_DEBUG] Said: %s: %s"), *Username, *Message);
	}

	static void CmdHelp(const TArray<FString>& Args, UWorld* World)
	{
		UE_LOG(LogTemp, Log, TEXT("==================== Stream Debug Commands ===================="));
		UE_LOG(LogTemp, Log, TEXT("  stream.reaction <Tag>       Fire reaction (Headshot, Multikill, AirDashKill, YankKill,"));
		UE_LOG(LogTemp, Log, TEXT("                              MeleeKill, ChainElectrify, AntennaDone, PlayerDeath,"));
		UE_LOG(LogTemp, Log, TEXT("                              HypeBurst, Boredom, ChannelSub, FriendSpoke)"));
		UE_LOG(LogTemp, Log, TEXT("  stream.scripted <SeqID>     Start scripted sequence (regulars_argue, antenna_drama,"));
		UE_LOG(LogTemp, Log, TEXT("                              first_kill_chat)"));
		UE_LOG(LogTemp, Log, TEXT("  stream.stopscripted         Stop all active scripted sequences"));
		UE_LOG(LogTemp, Log, TEXT("  stream.burst [N]            Fire HypeBurst N times (default 6)"));
		UE_LOG(LogTemp, Log, TEXT("  stream.say <user> <msg>     Inject a custom debug chat message"));
		UE_LOG(LogTemp, Log, TEXT("  stream.headshot             Quick shorthand for stream.reaction Headshot"));
		UE_LOG(LogTemp, Log, TEXT("  stream.multikill            Quick shorthand for Multikill"));
		UE_LOG(LogTemp, Log, TEXT("  stream.antenna              Quick shorthand for AntennaDone"));
		UE_LOG(LogTemp, Log, TEXT("  stream.death                Quick shorthand for PlayerDeath"));
		UE_LOG(LogTemp, Log, TEXT("  stream.boredom              Quick shorthand for Boredom"));
		UE_LOG(LogTemp, Log, TEXT("  stream.help                 This help"));
		UE_LOG(LogTemp, Log, TEXT("================================================================"));
	}
}

// ==================== Auto-registration ====================

static FAutoConsoleCommandWithWorldAndArgs CmdStreamReaction(
	TEXT("stream.reaction"),
	TEXT("Fire a chat reaction. Usage: stream.reaction <TagName>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ChatDebug::CmdReaction)
);

static FAutoConsoleCommandWithWorldAndArgs CmdStreamScripted(
	TEXT("stream.scripted"),
	TEXT("Run a scripted sequence. Usage: stream.scripted <SequenceID>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ChatDebug::CmdScripted)
);

static FAutoConsoleCommandWithWorldAndArgs CmdStreamStopScripted(
	TEXT("stream.stopscripted"),
	TEXT("Stop all active scripted sequences."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ChatDebug::CmdStopScripted)
);

static FAutoConsoleCommandWithWorldAndArgs CmdStreamBurst(
	TEXT("stream.burst"),
	TEXT("Fire a hype burst. Usage: stream.burst [count]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ChatDebug::CmdBurst)
);

static FAutoConsoleCommandWithWorldAndArgs CmdStreamSay(
	TEXT("stream.say"),
	TEXT("Inject a debug chat message. Usage: stream.say <Username> <Message...>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ChatDebug::CmdSay)
);

static FAutoConsoleCommandWithWorldAndArgs CmdStreamHelp(
	TEXT("stream.help"),
	TEXT("List all stream debug commands."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ChatDebug::CmdHelp)
);

// Quick shorthand commands — most common reactions
static FAutoConsoleCommandWithWorldAndArgs CmdStreamHeadshot(
	TEXT("stream.headshot"),
	TEXT("Quick: fire Chat.Event.Headshot reaction."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>&, UWorld* World)
	{
		ChatDebug::CmdQuickTag(TEXT("Headshot"), World);
	})
);

static FAutoConsoleCommandWithWorldAndArgs CmdStreamMultikill(
	TEXT("stream.multikill"),
	TEXT("Quick: fire Chat.Event.Multikill reaction."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>&, UWorld* World)
	{
		ChatDebug::CmdQuickTag(TEXT("Multikill"), World);
	})
);

static FAutoConsoleCommandWithWorldAndArgs CmdStreamAntenna(
	TEXT("stream.antenna"),
	TEXT("Quick: fire Chat.Event.AntennaDone reaction."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>&, UWorld* World)
	{
		ChatDebug::CmdQuickTag(TEXT("AntennaDone"), World);
	})
);

static FAutoConsoleCommandWithWorldAndArgs CmdStreamDeath(
	TEXT("stream.death"),
	TEXT("Quick: fire Chat.Event.PlayerDeath reaction (Schadenfreude)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>&, UWorld* World)
	{
		ChatDebug::CmdQuickTag(TEXT("PlayerDeath"), World);
	})
);

static FAutoConsoleCommandWithWorldAndArgs CmdStreamBoredom(
	TEXT("stream.boredom"),
	TEXT("Quick: fire Chat.Event.Boredom reaction."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>&, UWorld* World)
	{
		ChatDebug::CmdQuickTag(TEXT("Boredom"), World);
	})
);
