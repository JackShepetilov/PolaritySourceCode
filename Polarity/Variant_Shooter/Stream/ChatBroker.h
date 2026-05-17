// ChatBroker.h
// Coordinates all chat producers (ambient, reactions, scripted, hints, channel events,
// hype bursts, boredom, schadenfreude, friend voice echo, direct mentions) and dispatches
// FStreamChatMessage events at a rate-limited cadence.
//
// Owned by UStreamSubsystem (one broker per game instance). Event-driven and timer-driven
// — no Tick. Producers subscribe to delegates / spawn timers when relevant; the dispatcher
// runs a slow timer to flush the queue under the current per-second cap.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GameplayTagContainer.h"
#include "ChatTypes.h"
#include "StyleAction.h"
#include "ChatBroker.generated.h"

class UDataTable;
class UStreamSubsystem;
class UStyleComponent;
class AArenaManager;
class AArenaAntenna;
class USubtitleSubsystem;
class UStreamConfig;
struct FDonation;

UENUM(BlueprintType)
enum class EChatPhase : uint8
{
	Idle,       // No run active — nothing fires
	Opening,    // Scripted opening sequence only; all other producers suppressed
	Warmup,     // Ambient slower, reactions/hint/boredom OK, hype still off
	Normal      // Everything full speed
};

UCLASS(BlueprintType)
class POLARITY_API UChatBroker : public UObject
{
	GENERATED_BODY()

public:
	// ==================== Lifecycle ====================

	/** Called by UStreamSubsystem after construction; binds to subtitle + sets up timers. */
	void Init(UStreamSubsystem* InOwner);

	/** Tears down all subscriptions and timers. */
	void Shutdown();

	/** Apply config (DataTables + timing) from UStreamConfig. */
	void ApplyConfig(UStreamConfig* InConfig);

	// ==================== Per-arena binding ====================

	void BindStyleComponent(UStyleComponent* Style);
	void UnbindStyleComponent();

	void BindArenaManager(AArenaManager* Arena);
	void UnbindArenaManager();

	// ==================== Run lifecycle ====================

	/** Called by UStreamSubsystem on RunStarted. Sets phase to Opening and triggers the
	 *  appropriate opening scripted sequence (`stream_opening_first` vs `_normal`). */
	UFUNCTION(BlueprintCallable, Category = "Chat|Run")
	void BeginRun(bool bIsFirstRun);

	/** Called by UStreamSubsystem on RunEnded. Cancels transition timers + active sequences. */
	UFUNCTION(BlueprintCallable, Category = "Chat|Run")
	void EndRun();

	UFUNCTION(BlueprintPure, Category = "Chat|Run")
	EChatPhase GetCurrentPhase() const { return CurrentPhase; }

	// ==================== Public triggers ====================

	/** Fire a reaction by tag; broker picks a weighted random matching row from Reactions table. */
	UFUNCTION(BlueprintCallable, Category = "Chat")
	void EmitReaction(FGameplayTag EventTag);

	/** Start playing a scripted sequence by SequenceID. Multiple sequences can run in parallel. */
	UFUNCTION(BlueprintCallable, Category = "Chat")
	void RunScripted(FName SequenceID);

	UFUNCTION(BlueprintCallable, Category = "Chat")
	void StopAllScripted();

	/** Debug — force-enqueue a custom message. */
	UFUNCTION(BlueprintCallable, Category = "Chat|Debug")
	void DebugEmit(const FString& Username, const FText& Message, EChatMessageKind Kind);

	// ==================== Events ====================

	UPROPERTY(BlueprintAssignable, Category = "Chat|Events")
	FOnStreamChatMessageReady OnChatMessageReady;

protected:
	// ==================== Event handlers ====================

	UFUNCTION() void HandleLikesGenerated(int32 LikeCount, FVector WorldLocation);
	UFUNCTION() void HandleStyleActionRegistered(const FStyleAction& Action);
	UFUNCTION() void HandleAntennaActivated(AArenaAntenna* Antenna);
	UFUNCTION() void HandleSubtitleStarted(const FText& Text, float Duration);

	// ==================== Timer callbacks ====================

	void TickAmbient();
	void TickChannelEvent();
	void TickHint();
	void TickBoredom();
	void TickDispatcher();
	void TickScriptedStep(FName SequenceID);

	// ==================== Internals ====================

	void RescheduleAmbient();
	void RescheduleChannelEvent();
	void RescheduleHint();
	void RescheduleBoredom();
	void RescheduleDispatcher();

	void TransitionToWarmup();
	void TransitionToNormal();

	/** Resolve final speaker (username + color) from PersonaRow / Override / random. */
	void ResolveSpeaker(const FName& PersonaRow, const FString& UsernameOverride, FString& OutUsername, FColor& OutColor) const;

	FString PickRandomNick() const;
	FColor PickRandomColor() const;

	void Enqueue(const FStreamChatMessage& Msg, int32 Priority);

	FStreamChatMessage MakeMessage(const FName& PersonaRow, const FString& UsernameOverride, const FText& Message, EChatMessageKind Kind) const;

	float CurrentMaxMessagesPerSecond() const;
	float SampleLikesPerSecond() const;

	/** Build {PlayerStreamerName}-substituted message text. Used for direct mentions. */
	FText SubstitutePlayerName(const FText& Source) const;

	// ==================== State ====================

	UPROPERTY(Transient) TWeakObjectPtr<UStreamSubsystem> Owner;
	UPROPERTY(Transient) TWeakObjectPtr<UStreamConfig> Config;
	UPROPERTY(Transient) TWeakObjectPtr<UStyleComponent> BoundStyle;
	UPROPERTY(Transient) TWeakObjectPtr<AArenaManager> BoundArena;

	UPROPERTY(Transient) TObjectPtr<UDataTable> AmbientTable;
	UPROPERTY(Transient) TObjectPtr<UDataTable> ReactionsTable;
	UPROPERTY(Transient) TObjectPtr<UDataTable> ScriptedTable;
	UPROPERTY(Transient) TObjectPtr<UDataTable> HintsTable;
	UPROPERTY(Transient) TObjectPtr<UDataTable> PersonasTable;

	// Timers
	FTimerHandle AmbientHandle;
	FTimerHandle ChannelEventHandle;
	FTimerHandle HintHandle;
	FTimerHandle BoredomHandle;
	FTimerHandle DispatcherHandle;

	// Phase transition timers
	FTimerHandle OpeningTransitionHandle;
	FTimerHandle WarmupTransitionHandle;

	UPROPERTY(Transient)
	EChatPhase CurrentPhase = EChatPhase::Idle;

	/** Captured at BeginRun and used by transition callbacks to pick first-vs-normal durations. */
	UPROPERTY(Transient)
	bool bCurrentRunIsFirst = false;

	struct FQueuedMessage
	{
		FStreamChatMessage Msg;
		int32 Priority = 0;
	};
	TArray<FQueuedMessage> Queue;

	struct FActiveScripted
	{
		FName SequenceID;
		TArray<FChatScriptedRow> Steps;   // cached copies of rows ordered by StepIndex
		int32 NextStepIndex = 0;
		FTimerHandle StepHandle;
	};
	TArray<FActiveScripted> ActiveScripted;

	double LastEmitTimeSeconds = 0.0;
	float LastObservedLPS = 0.0f;

	double LastNonBoredomActivitySeconds = 0.0;
	bool bBoredomActive = false;
};
