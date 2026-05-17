// ChatBroker.cpp
// MVP implementation: Ambient + Reactive + Hint + HypeBurst + Boredom + Schadenfreude + Dispatcher.
// Scripted / FriendEcho / ChannelEvents / DirectMention are stubbed (logged) for a later pass.
// Logging tag: [STREAM_DEBUG].

#include "ChatBroker.h"

#include "StreamSubsystem.h"
#include "StreamConfig.h"
#include "StreamArenaConfig.h"
#include "StyleComponent.h"
#include "StyleAction.h"
#include "Polarity/Arena/ArenaManager.h"
#include "Polarity/Arena/ArenaAntenna.h"
#include "Polarity/Subtitle/SubtitleSubsystem.h"
#include "Polarity/Variant_Shooter/Lore/LoreSubsystem.h"
#include "Polarity/Variant_Shooter/Lore/LoreTypes.h"

#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameplayTagContainer.h"
#include "TimerManager.h"

namespace ChatBrokerInternal
{
	static const TArray<FString>& RandomNickPool()
	{
		static const TArray<FString> Pool = {
			TEXT("leetgamer1337"), TEXT("xxstanXxx"), TEXT("just_a_lurker"),
			TEXT("AnonViewer42"), TEXT("ChromeNinja"), TEXT("byte_witch"),
			TEXT("PolarityFan"), TEXT("8bit_ghost"), TEXT("toxic_grandma"),
			TEXT("dr_robotnik"), TEXT("midnight_owl"), TEXT("based_dept"),
			TEXT("vibe_check_pro"), TEXT("crypto_bro_999"), TEXT("emote_addict"),
			TEXT("ratio_engineer"), TEXT("def_not_a_bot"), TEXT("StreamBot42"),
			TEXT("notch__"), TEXT("regen_warrior"), TEXT("ohnoitschad"),
			TEXT("cope_dealer"), TEXT("luke_warm_take"), TEXT("hexstreamer"),
			TEXT("kappa_kappa"), TEXT("dollar_signs"), TEXT("doge_vault"),
			TEXT("ice_cream_man"), TEXT("4head_4ever"), TEXT("ramen_andy")
		};
		return Pool;
	}

	static const TArray<FColor>& RandomColorPool()
	{
		static const TArray<FColor> Pool = {
			FColor(255, 140, 0),   FColor(255, 102, 204), FColor(102, 204, 255),
			FColor(255, 204, 0),   FColor(102, 255, 178), FColor(204, 102, 255),
			FColor(255, 153, 153), FColor(153, 255, 102), FColor(102, 102, 255),
			FColor(255, 255, 255), FColor(200, 200, 200)
		};
		return Pool;
	}

	/** Map EStyleCategory enum to chat reaction tag name. */
	static FName StyleCategoryToReactionTagName(EStyleCategory Category)
	{
		switch (Category)
		{
		case EStyleCategory::Headshot:           return TEXT("Chat.Event.Headshot");
		case EStyleCategory::Multikill:          return TEXT("Chat.Event.Multikill");
		case EStyleCategory::AirDashKill:        return TEXT("Chat.Event.AirDashKill");
		case EStyleCategory::YankKill:           return TEXT("Chat.Event.YankKill");
		case EStyleCategory::SlideKill:          return TEXT("Chat.Event.SlideKill");
		case EStyleCategory::MeleeKill:          return TEXT("Chat.Event.MeleeKill");
		case EStyleCategory::ChainElectrify:     return TEXT("Chat.Event.ChainElectrify");
		case EStyleCategory::EnvironmentalKill:  return TEXT("Chat.Event.EnvironmentalKill");
		case EStyleCategory::ParryReflect:       return TEXT("Chat.Event.ParryReflect");
		case EStyleCategory::NoHitClear:         return TEXT("Chat.Event.NoHitClear");
		default:                                 return NAME_None;
		}
	}
}

// ==================== Lifecycle ====================

void UChatBroker::Init(UStreamSubsystem* InOwner)
{
	Owner = InOwner;
	LastEmitTimeSeconds = 0.0;
	bBoredomActive = false;
	LastNonBoredomActivitySeconds = (GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0);

	if (UGameInstance* GI = (InOwner ? InOwner->GetGameInstance() : nullptr))
	{
		if (USubtitleSubsystem* Subs = GI->GetSubsystem<USubtitleSubsystem>())
		{
			Subs->OnSubtitleStarted.AddDynamic(this, &UChatBroker::HandleSubtitleStarted);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[STREAM_DEBUG] ChatBroker initialized"));
}

void UChatBroker::Shutdown()
{
	if (UWorld* W = GetWorld())
	{
		FTimerManager& TM = W->GetTimerManager();
		TM.ClearTimer(AmbientHandle);
		TM.ClearTimer(ChannelEventHandle);
		TM.ClearTimer(HintHandle);
		TM.ClearTimer(BoredomHandle);
		TM.ClearTimer(DispatcherHandle);
		for (FActiveScripted& A : ActiveScripted)
		{
			TM.ClearTimer(A.StepHandle);
		}
	}
	ActiveScripted.Reset();
	Queue.Reset();

	UnbindStyleComponent();
	UnbindArenaManager();

	if (UGameInstance* GI = (Owner.IsValid() ? Owner->GetGameInstance() : nullptr))
	{
		if (USubtitleSubsystem* Subs = GI->GetSubsystem<USubtitleSubsystem>())
		{
			Subs->OnSubtitleStarted.RemoveDynamic(this, &UChatBroker::HandleSubtitleStarted);
		}
	}
}

void UChatBroker::ApplyConfig(UStreamConfig* InConfig)
{
	Config = InConfig;
	if (!InConfig)
	{
		return;
	}

	AmbientTable    = InConfig->ChatAmbientTable;
	ReactionsTable  = InConfig->ChatReactionsTable;
	ScriptedTable   = InConfig->ChatScriptedTable;
	HintsTable      = InConfig->ChatHintsTable;
	PersonasTable   = InConfig->ChatPersonasTable;

	RescheduleAmbient();
	RescheduleChannelEvent();
	RescheduleHint();
	RescheduleBoredom();
	RescheduleDispatcher();

	UE_LOG(LogTemp, Log, TEXT("[STREAM_DEBUG] ChatBroker config applied"));
}

// ==================== Subscriptions ====================

void UChatBroker::BindStyleComponent(UStyleComponent* Style)
{
	UnbindStyleComponent();
	BoundStyle = Style;
	if (Style)
	{
		Style->OnStyleActionRegistered.AddDynamic(this, &UChatBroker::HandleStyleActionRegistered);
		Style->OnLikesGenerated.AddDynamic(this, &UChatBroker::HandleLikesGenerated);
	}
}

void UChatBroker::UnbindStyleComponent()
{
	if (UStyleComponent* Style = BoundStyle.Get())
	{
		Style->OnStyleActionRegistered.RemoveDynamic(this, &UChatBroker::HandleStyleActionRegistered);
		Style->OnLikesGenerated.RemoveDynamic(this, &UChatBroker::HandleLikesGenerated);
	}
	BoundStyle = nullptr;
}

void UChatBroker::BindArenaManager(AArenaManager* Arena)
{
	UnbindArenaManager();
	BoundArena = Arena;
	if (Arena)
	{
		Arena->OnAntennaActivated.AddDynamic(this, &UChatBroker::HandleAntennaActivated);
	}
}

void UChatBroker::UnbindArenaManager()
{
	if (AArenaManager* Arena = BoundArena.Get())
	{
		Arena->OnAntennaActivated.RemoveDynamic(this, &UChatBroker::HandleAntennaActivated);
	}
	BoundArena = nullptr;
}

// ==================== Event handlers ====================

void UChatBroker::HandleStyleActionRegistered(const FStyleAction& Action)
{
	const FName TagName = ChatBrokerInternal::StyleCategoryToReactionTagName(Action.Category);
	if (TagName.IsNone())
	{
		return;
	}
	const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TagName, false);
	if (Tag.IsValid())
	{
		EmitReaction(Tag);
	}

	LastNonBoredomActivitySeconds = (GetWorld() ? GetWorld()->GetTimeSeconds() : LastNonBoredomActivitySeconds);
}

void UChatBroker::HandleLikesGenerated(int32 LikeCount, FVector WorldLocation)
{
	// HypeBurst: detect LPS jump across HighLPSThreshold.
	const float CurrentLPS = SampleLikesPerSecond();
	UStreamConfig* Cfg = Config.Get();
	const float Threshold = Cfg ? Cfg->ChatHighLPSThreshold : 50.0f;

	if (LastObservedLPS < Threshold && CurrentLPS >= Threshold)
	{
		const FGameplayTag HypeTag = FGameplayTag::RequestGameplayTag(FName(TEXT("Chat.Event.HypeBurst")), false);
		if (HypeTag.IsValid() && ReactionsTable)
		{
			const int32 BurstSize = Cfg ? Cfg->ChatHypeBurstSize : 6;
			for (int32 i = 0; i < BurstSize; ++i)
			{
				EmitReaction(HypeTag);
			}
		}
	}
	LastObservedLPS = CurrentLPS;
}

void UChatBroker::HandleAntennaActivated(AArenaAntenna* Antenna)
{
	// Generic "antenna done" reaction (random pool from DT_ChatReactions)
	const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Chat.Event.AntennaDone")), false);
	if (Tag.IsValid())
	{
		EmitReaction(Tag);
	}

	// Lore-specific commentary (scripted sequence from DT_Lore_*)
	if (!Antenna || !Owner.IsValid()) { return; }

	UGameInstance* GI = Owner->GetGameInstance();
	if (!GI) { return; }

	ULoreSubsystem* Lore = GI->GetSubsystem<ULoreSubsystem>();
	if (!Lore) { return; }

	FName Biome = NAME_None;
	if (UStreamArenaConfig* AC = Owner->GetArenaConfig())
	{
		Biome = AC->Biome;
	}

	FLoreEntryRow Entry;
	if (Lore->PickAndConsumeLoreForArena(Antenna->ArenaTagForLore, Biome, Entry))
	{
		if (!Entry.ChatScriptedSequenceID.IsNone())
		{
			RunScripted(Entry.ChatScriptedSequenceID);
		}
		// TODO when voice integration lands: trigger USubtitleSubsystem with Entry.VoiceLineID
	}
}

void UChatBroker::HandleSubtitleStarted(const FText& Text, float Duration)
{
	// MVP stub: FriendEcho producer not yet implemented.
	// Next phase: pick from Reactions with EventTag = Chat.Event.FriendSpoke.
}

// ==================== Timer callbacks ====================

void UChatBroker::TickAmbient()
{
	if (!AmbientTable)
	{
		RescheduleAmbient();
		return;
	}

	// Weighted random pick across all rows.
	TArray<FName> RowNames = AmbientTable->GetRowNames();
	float TotalWeight = 0.0f;
	for (const FName& RowName : RowNames)
	{
		if (const FChatAmbientRow* Row = AmbientTable->FindRow<FChatAmbientRow>(RowName, TEXT("AmbientTick")))
		{
			TotalWeight += FMath::Max(0.0f, Row->Weight);
		}
	}
	if (TotalWeight <= 0.0f)
	{
		RescheduleAmbient();
		return;
	}

	float Roll = FMath::FRand() * TotalWeight;
	for (const FName& RowName : RowNames)
	{
		const FChatAmbientRow* Row = AmbientTable->FindRow<FChatAmbientRow>(RowName, TEXT("AmbientTick"));
		if (!Row) { continue; }
		Roll -= FMath::Max(0.0f, Row->Weight);
		if (Roll <= 0.0f)
		{
			Enqueue(MakeMessage(Row->PersonaRow, Row->UsernameOverride, Row->Message, EChatMessageKind::Ambient), /*Priority*/ 0);
			break;
		}
	}

	RescheduleAmbient();
}

void UChatBroker::TickChannelEvent()
{
	// MVP stub: fires a Chat.Event.ChannelSub from reactions table if available.
	const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Chat.Event.ChannelSub")), false);
	if (Tag.IsValid())
	{
		EmitReaction(Tag);
	}
	RescheduleChannelEvent();
}

void UChatBroker::TickHint()
{
	// MVP: random hint from table, no XP analysis (later phase).
	if (!HintsTable)
	{
		RescheduleHint();
		return;
	}

	TArray<FName> RowNames = HintsTable->GetRowNames();
	float TotalWeight = 0.0f;
	for (const FName& RowName : RowNames)
	{
		if (const FChatHintRow* Row = HintsTable->FindRow<FChatHintRow>(RowName, TEXT("HintTick")))
		{
			TotalWeight += FMath::Max(0.0f, Row->Weight);
		}
	}
	if (TotalWeight <= 0.0f)
	{
		RescheduleHint();
		return;
	}

	float Roll = FMath::FRand() * TotalWeight;
	for (const FName& RowName : RowNames)
	{
		const FChatHintRow* Row = HintsTable->FindRow<FChatHintRow>(RowName, TEXT("HintTick"));
		if (!Row) { continue; }
		Roll -= FMath::Max(0.0f, Row->Weight);
		if (Roll <= 0.0f)
		{
			Enqueue(MakeMessage(Row->PersonaRow, Row->UsernameOverride, Row->Message, EChatMessageKind::Hint), /*Priority*/ 2);
			break;
		}
	}

	RescheduleHint();
}

void UChatBroker::TickBoredom()
{
	UStreamConfig* Cfg = Config.Get();
	const float Activation = Cfg ? Cfg->ChatBoredomActivationDelaySec : 30.0f;

	const float Now = (GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f);
	const float SinceActivity = Now - static_cast<float>(LastNonBoredomActivitySeconds);
	const float CurrentLPS = SampleLikesPerSecond();

	if (CurrentLPS <= 0.05f && SinceActivity >= Activation)
	{
		bBoredomActive = true;
		const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Chat.Event.Boredom")), false);
		if (Tag.IsValid())
		{
			EmitReaction(Tag);
		}
	}
	else
	{
		bBoredomActive = false;
	}

	RescheduleBoredom();
}

void UChatBroker::TickDispatcher()
{
	if (Queue.Num() == 0)
	{
		RescheduleDispatcher();
		return;
	}

	const double Now = (GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0);
	const float MaxPerSec = CurrentMaxMessagesPerSecond();
	const double MinGap = (MaxPerSec > 0.0f) ? (1.0 / MaxPerSec) : 0.5;

	if (Now - LastEmitTimeSeconds >= MinGap)
	{
		// Pop highest priority (largest Priority number first); FIFO within same priority.
		int32 BestIdx = 0;
		for (int32 i = 1; i < Queue.Num(); ++i)
		{
			if (Queue[i].Priority > Queue[BestIdx].Priority)
			{
				BestIdx = i;
			}
		}
		const FQueuedMessage Popped = Queue[BestIdx];
		Queue.RemoveAt(BestIdx);
		LastEmitTimeSeconds = Now;
		OnChatMessageReady.Broadcast(Popped.Msg);
	}

	RescheduleDispatcher();
}

void UChatBroker::TickScriptedStep(FName SequenceID)
{
	const int32 Idx = ActiveScripted.IndexOfByPredicate([SequenceID](const FActiveScripted& A)
	{
		return A.SequenceID == SequenceID;
	});
	if (Idx == INDEX_NONE) { return; }

	FActiveScripted& Active = ActiveScripted[Idx];
	if (!Active.Steps.IsValidIndex(Active.NextStepIndex))
	{
		ActiveScripted.RemoveAt(Idx);
		return;
	}

	const FChatScriptedRow& Step = Active.Steps[Active.NextStepIndex];
	Enqueue(MakeMessage(Step.PersonaRow, Step.UsernameOverride, Step.Message, EChatMessageKind::Scripted),
		/*Priority*/ 2);

	Active.NextStepIndex++;

	if (Active.NextStepIndex >= Active.Steps.Num())
	{
		UE_LOG(LogTemp, Log, TEXT("[STREAM_DEBUG] Scripted '%s' finished"), *SequenceID.ToString());
		ActiveScripted.RemoveAt(Idx);
		return;
	}

	UWorld* W = GetWorld();
	if (!W) { return; }

	const float NextDelay = FMath::Max(0.001f, Active.Steps[Active.NextStepIndex].DelaySec);
	W->GetTimerManager().SetTimer(
		Active.StepHandle,
		FTimerDelegate::CreateUObject(this, &UChatBroker::TickScriptedStep, SequenceID),
		NextDelay, false);
}

// ==================== Triggers ====================

void UChatBroker::EmitReaction(FGameplayTag EventTag)
{
	if (!ReactionsTable || !EventTag.IsValid())
	{
		return;
	}

	TArray<FName> RowNames = ReactionsTable->GetRowNames();
	float TotalWeight = 0.0f;
	for (const FName& RowName : RowNames)
	{
		if (const FChatReactionRow* Row = ReactionsTable->FindRow<FChatReactionRow>(RowName, TEXT("EmitReaction")))
		{
			if (Row->EventTag == EventTag)
			{
				TotalWeight += FMath::Max(0.0f, Row->Weight);
			}
		}
	}
	if (TotalWeight <= 0.0f)
	{
		return;
	}

	float Roll = FMath::FRand() * TotalWeight;
	for (const FName& RowName : RowNames)
	{
		const FChatReactionRow* Row = ReactionsTable->FindRow<FChatReactionRow>(RowName, TEXT("EmitReaction"));
		if (!Row || Row->EventTag != EventTag) { continue; }
		Roll -= FMath::Max(0.0f, Row->Weight);
		if (Roll <= 0.0f)
		{
			EChatMessageKind Kind = EChatMessageKind::Reaction;
			const FString TagStr = EventTag.ToString();
			if (TagStr.Contains(TEXT("HypeBurst")))         { Kind = EChatMessageKind::Hype; }
			else if (TagStr.Contains(TEXT("Boredom")))      { Kind = EChatMessageKind::Boredom; }
			else if (TagStr.Contains(TEXT("PlayerDeath")))  { Kind = EChatMessageKind::Schadenfreude; }
			else if (TagStr.Contains(TEXT("ChannelSub"))
				  || TagStr.Contains(TEXT("ChannelFollow")))  { Kind = EChatMessageKind::Channel; }
			else if (TagStr.Contains(TEXT("FriendSpoke")))    { Kind = EChatMessageKind::FriendEcho; }
			else if (TagStr.Contains(TEXT("DirectMention"))) { Kind = EChatMessageKind::DirectMention; }

			Enqueue(MakeMessage(Row->PersonaRow, Row->UsernameOverride, SubstitutePlayerName(Row->Message), Kind),
				/*Priority*/ (Kind == EChatMessageKind::Schadenfreude || Kind == EChatMessageKind::Hype) ? 3 : 1);
			break;
		}
	}
}

void UChatBroker::RunScripted(FName SequenceID)
{
	if (!ScriptedTable || SequenceID.IsNone()) { return; }

	// Already running this sequence? Guard against double-trigger.
	const int32 ExistingIdx = ActiveScripted.IndexOfByPredicate([SequenceID](const FActiveScripted& A)
	{
		return A.SequenceID == SequenceID;
	});
	if (ExistingIdx != INDEX_NONE)
	{
		UE_LOG(LogTemp, Log, TEXT("[STREAM_DEBUG] Scripted '%s' already running — ignoring duplicate trigger"), *SequenceID.ToString());
		return;
	}

	// Gather rows matching SequenceID
	TArray<const FChatScriptedRow*> Steps;
	for (const FName& RowName : ScriptedTable->GetRowNames())
	{
		const FChatScriptedRow* Row = ScriptedTable->FindRow<FChatScriptedRow>(RowName, TEXT("RunScripted"));
		if (Row && Row->SequenceID == SequenceID)
		{
			Steps.Add(Row);
		}
	}

	if (Steps.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[STREAM_DEBUG] No scripted rows for SequenceID '%s'"), *SequenceID.ToString());
		return;
	}

	// Sort by StepIndex ascending
	Steps.Sort([](const FChatScriptedRow& A, const FChatScriptedRow& B)
	{
		return A.StepIndex < B.StepIndex;
	});

	FActiveScripted Active;
	Active.SequenceID = SequenceID;
	Active.NextStepIndex = 0;
	Active.Steps.Reserve(Steps.Num());
	for (const FChatScriptedRow* Row : Steps)
	{
		Active.Steps.Add(*Row);
	}
	ActiveScripted.Add(MoveTemp(Active));

	UWorld* W = GetWorld();
	if (!W) { return; }

	const float FirstDelay = FMath::Max(0.001f, ActiveScripted.Last().Steps[0].DelaySec);
	W->GetTimerManager().SetTimer(
		ActiveScripted.Last().StepHandle,
		FTimerDelegate::CreateUObject(this, &UChatBroker::TickScriptedStep, SequenceID),
		FirstDelay, false);

	UE_LOG(LogTemp, Log, TEXT("[STREAM_DEBUG] Started scripted '%s' (%d steps, first delay %.2fs)"),
		*SequenceID.ToString(), Steps.Num(), FirstDelay);
}

void UChatBroker::StopAllScripted()
{
	if (UWorld* W = GetWorld())
	{
		for (FActiveScripted& A : ActiveScripted)
		{
			W->GetTimerManager().ClearTimer(A.StepHandle);
		}
	}
	ActiveScripted.Reset();
}

void UChatBroker::DebugEmit(const FString& Username, const FText& Message, EChatMessageKind Kind)
{
	FStreamChatMessage Msg;
	Msg.Username = Username;
	Msg.Message = Message;
	Msg.UsernameColor = FColor::Yellow;
	Msg.Kind = Kind;
	OnChatMessageReady.Broadcast(Msg);
}

// ==================== Internals ====================

void UChatBroker::ResolveSpeaker(const FName& PersonaRow, const FString& UsernameOverride, FString& OutUsername, FColor& OutColor) const
{
	OutUsername.Empty();
	OutColor = FColor::White;

	if (PersonasTable && !PersonaRow.IsNone())
	{
		if (const FChatPersonaRow* Persona = PersonasTable->FindRow<FChatPersonaRow>(PersonaRow, TEXT("ResolveSpeaker")))
		{
			OutUsername = Persona->Username;
			OutColor = Persona->Color;
			return;
		}
	}

	if (!UsernameOverride.IsEmpty())
	{
		OutUsername = UsernameOverride;
		OutColor = PickRandomColor();
		return;
	}

	OutUsername = PickRandomNick();
	OutColor = PickRandomColor();
}

FString UChatBroker::PickRandomNick() const
{
	const TArray<FString>& Pool = ChatBrokerInternal::RandomNickPool();
	return Pool[FMath::RandRange(0, Pool.Num() - 1)];
}

FColor UChatBroker::PickRandomColor() const
{
	const TArray<FColor>& Pool = ChatBrokerInternal::RandomColorPool();
	return Pool[FMath::RandRange(0, Pool.Num() - 1)];
}

void UChatBroker::Enqueue(const FStreamChatMessage& Msg, int32 Priority)
{
	FQueuedMessage Q;
	Q.Msg = Msg;
	Q.Priority = Priority;
	Queue.Add(Q);
}

FStreamChatMessage UChatBroker::MakeMessage(const FName& PersonaRow, const FString& UsernameOverride, const FText& Message, EChatMessageKind Kind) const
{
	FStreamChatMessage Msg;
	Msg.Message = Message;
	Msg.Kind = Kind;
	ResolveSpeaker(PersonaRow, UsernameOverride, Msg.Username, Msg.UsernameColor);
	return Msg;
}

float UChatBroker::CurrentMaxMessagesPerSecond() const
{
	UStreamConfig* Cfg = Config.Get();
	if (!Cfg)
	{
		return 2.0f;
	}

	const float CurrentLPS = SampleLikesPerSecond();
	return (CurrentLPS >= Cfg->ChatHighLPSThreshold)
		? static_cast<float>(Cfg->ChatMaxMessagesPerSecondHigh)
		: static_cast<float>(Cfg->ChatMaxMessagesPerSecondLow);
}

float UChatBroker::SampleLikesPerSecond() const
{
	if (UStyleComponent* Style = BoundStyle.Get())
	{
		return Style->GetLikesPerSecond();
	}
	return 0.0f;
}

FText UChatBroker::SubstitutePlayerName(const FText& Source) const
{
	if (!Owner.IsValid())
	{
		return Source;
	}
	const FString Src = Source.ToString();
	if (!Src.Contains(TEXT("{PlayerName}")))
	{
		return Source;
	}
	const FString Substituted = Src.Replace(TEXT("{PlayerName}"), *Owner->GetPlayerStreamerName());
	return FText::FromString(Substituted);
}

// ==================== Rescheduling ====================

void UChatBroker::RescheduleAmbient()
{
	UWorld* W = GetWorld();
	if (!W) { return; }
	UStreamConfig* Cfg = Config.Get();
	const float Min = Cfg ? Cfg->ChatAmbientIntervalMin : 2.0f;
	const float Max = Cfg ? Cfg->ChatAmbientIntervalMax : 5.0f;
	const float Delay = FMath::FRandRange(Min, FMath::Max(Min, Max));
	W->GetTimerManager().SetTimer(AmbientHandle, FTimerDelegate::CreateUObject(this, &UChatBroker::TickAmbient), Delay, /*bLoop*/false);
}

void UChatBroker::RescheduleChannelEvent()
{
	UWorld* W = GetWorld();
	if (!W) { return; }
	UStreamConfig* Cfg = Config.Get();
	const float Min = Cfg ? Cfg->ChatChannelEventIntervalMin : 60.0f;
	const float Max = Cfg ? Cfg->ChatChannelEventIntervalMax : 180.0f;
	const float Delay = FMath::FRandRange(Min, FMath::Max(Min, Max));
	W->GetTimerManager().SetTimer(ChannelEventHandle, FTimerDelegate::CreateUObject(this, &UChatBroker::TickChannelEvent), Delay, /*bLoop*/false);
}

void UChatBroker::RescheduleHint()
{
	UWorld* W = GetWorld();
	if (!W) { return; }
	UStreamConfig* Cfg = Config.Get();
	const float Delay = Cfg ? Cfg->ChatHintCheckIntervalSec : 20.0f;
	W->GetTimerManager().SetTimer(HintHandle, FTimerDelegate::CreateUObject(this, &UChatBroker::TickHint), Delay, /*bLoop*/false);
}

void UChatBroker::RescheduleBoredom()
{
	UWorld* W = GetWorld();
	if (!W) { return; }
	UStreamConfig* Cfg = Config.Get();
	const float Delay = Cfg ? Cfg->ChatBoredomCheckIntervalSec : 8.0f;
	W->GetTimerManager().SetTimer(BoredomHandle, FTimerDelegate::CreateUObject(this, &UChatBroker::TickBoredom), Delay, /*bLoop*/false);
}

void UChatBroker::RescheduleDispatcher()
{
	UWorld* W = GetWorld();
	if (!W) { return; }
	const float Delay = (Queue.Num() > 0) ? 0.1f : 0.5f;
	W->GetTimerManager().SetTimer(DispatcherHandle, FTimerDelegate::CreateUObject(this, &UChatBroker::TickDispatcher), Delay, /*bLoop*/false);
}
