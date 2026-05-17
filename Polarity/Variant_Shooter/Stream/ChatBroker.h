// ChatBroker.h
// Coordinates all chat producers (ambient, reactions, scripted, hints, channel events,
// hype bursts, boredom, schadenfreude, friend voice echo, direct mentions) and dispatches
// FChatMessage events at a rate-limited cadence.
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
	FOnChatMessageReady OnChatMessageReady;

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
	void TickScriptedStep(int32 SequenceSlot);

	// ==================== Internals ====================

	void RescheduleAmbient();
	void RescheduleChannelEvent();
	void RescheduleHint();
	void RescheduleBoredom();
	void RescheduleDispatcher();

	/** Resolve final speaker (username + color) from PersonaRow / Override / random. */
	void ResolveSpeaker(const FName& PersonaRow, const FString& UsernameOverride, FString& OutUsername, FColor& OutColor) const;

	FString PickRandomNick() const;
	FColor PickRandomColor() const;

	void Enqueue(const FChatMessage& Msg, int32 Priority);

	FChatMessage MakeMessage(const FName& PersonaRow, const FString& UsernameOverride, const FText& Message, EChatMessageKind Kind) const;

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

	struct FQueuedMessage
	{
		FChatMessage Msg;
		int32 Priority = 0;
	};
	TArray<FQueuedMessage> Queue;

	struct FActiveScripted
	{
		FName SequenceID;
		int32 NextStepIndex = 0;
		FTimerHandle StepHandle;
	};
	TArray<FActiveScripted> ActiveScripted;

	double LastEmitTimeSeconds = 0.0;
	float LastObservedLPS = 0.0f;

	double LastNonBoredomActivitySeconds = 0.0;
	bool bBoredomActive = false;
};
