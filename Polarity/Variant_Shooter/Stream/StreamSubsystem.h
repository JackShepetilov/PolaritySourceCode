// StreamSubsystem.h
// GameInstance subsystem owning the streaming layer state — viewers, donations,
// and run-scoped meta currency.
//
// Subscribes to URunSubsystem lifecycle (OnRunStarted/Ended/ArenaEntered). Reads
// LikesPerSecond from the player's UStyleComponent each tick and drives ViewerTarget
// via UStreamConfig curves. Donations are rolled per second based on current viewer
// count and broadcast for UI consumption.
//
// Meta donation currency persists across runs via UPROPERTY(SaveGame); SaveGame
// integration itself lands in a later phase.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "RunSubsystem.h"
#include "StreamSubsystem.generated.h"

class UStreamConfig;
class UStreamArenaConfig;
class UStyleComponent;
class UChatBroker;

USTRUCT(BlueprintType)
struct FDonation
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Stream")
	int32 Amount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Stream")
	FString DonorName;

	UPROPERTY(BlueprintReadOnly, Category = "Stream")
	FText Message;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnViewersChanged, int32, NewViewers);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDonationGenerated, const FDonation&, Donation);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMetaCurrencyChanged, int64, NewTotal);

UCLASS()
class POLARITY_API UStreamSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	// ==================== Subsystem lifecycle ====================

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ==================== FTickableGameObject ====================

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;
	virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Conditional; }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return false; }

	// ==================== Config ====================

	UFUNCTION(BlueprintCallable, Category = "Stream")
	void SetConfig(UStreamConfig* InConfig);

	UFUNCTION(BlueprintCallable, Category = "Stream")
	void SetArenaConfig(UStreamArenaConfig* InArenaConfig);

	/** Register the player's StyleComponent so the subsystem can poll LikesPerSecond. */
	UFUNCTION(BlueprintCallable, Category = "Stream")
	void RegisterStyleComponent(UStyleComponent* InStyle);

	// ==================== Chat + Player Name ====================

	UFUNCTION(BlueprintPure, Category = "Stream|Chat")
	UChatBroker* GetChatBroker() const { return ChatBroker; }

	UFUNCTION(BlueprintPure, Category = "Stream|Chat")
	UStyleComponent* GetStyleComponent() const { return StyleComponent.Get(); }

	UFUNCTION(BlueprintPure, Category = "Stream")
	UStreamConfig* GetConfig() const { return Config.Get(); }

	UFUNCTION(BlueprintPure, Category = "Stream")
	UStreamArenaConfig* GetArenaConfig() const { return ArenaConfig.Get(); }

	UFUNCTION(BlueprintPure, Category = "Stream|Player")
	FString GetPlayerStreamerName() const { return PlayerStreamerName; }

	UFUNCTION(BlueprintCallable, Category = "Stream|Player")
	void SetPlayerStreamerName(const FString& InName);

	// ==================== Read API ====================

	UFUNCTION(BlueprintPure, Category = "Stream")
	int32 GetViewers() const { return CurrentViewers; }

	UFUNCTION(BlueprintPure, Category = "Stream")
	int32 GetViewerTarget() const { return ViewerTarget; }

	UFUNCTION(BlueprintPure, Category = "Stream")
	float GetRunElapsedSeconds() const;

	UFUNCTION(BlueprintPure, Category = "Stream")
	int64 GetMetaCurrency() const { return MetaCurrency; }

	// ==================== Meta currency ====================

	/** Add to meta currency (called internally on donation; exposed for tooling/cheats). */
	UFUNCTION(BlueprintCallable, Category = "Stream|Meta")
	void AddMetaCurrency(int64 Amount);

	/** Spend meta currency. Returns false if insufficient. */
	UFUNCTION(BlueprintCallable, Category = "Stream|Meta")
	bool SpendMetaCurrency(int64 Amount);

	// ==================== Events ====================

	UPROPERTY(BlueprintAssignable, Category = "Stream|Events")
	FOnViewersChanged OnViewersChanged;

	UPROPERTY(BlueprintAssignable, Category = "Stream|Events")
	FOnDonationGenerated OnDonationGenerated;

	UPROPERTY(BlueprintAssignable, Category = "Stream|Events")
	FOnMetaCurrencyChanged OnMetaCurrencyChanged;

protected:
	// ==================== Run lifecycle handlers ====================

	UFUNCTION() void HandleRunStarted();
	UFUNCTION() void HandleRunEnded(ERunEndReason Reason);
	UFUNCTION() void HandleArenaEntered(int32 ArenaIndex);

	// ==================== Tick driver ====================

	/** Pull current LikesPerSecond from registered StyleComponent (0 if none). */
	float SampleLikesPerSecond() const;

	/** Recompute ViewerTarget from BaselineCurve(t), RankMultiplierCurve(LPS), ArenaMultiplier. */
	void RecomputeViewerTarget(float TimeIntoRunSeconds, float LikesPerSecond);

	/** Smooth Viewers toward ViewerTarget. */
	void UpdateViewers(float DeltaTime);

	/** Roll for and possibly spawn a donation this tick. */
	void TickDonations(float DeltaTime, float LikesPerSecond);

	/** Sample a random donor name from config pool. */
	FString PickDonorName() const;

	URunSubsystem* GetRunSubsystem() const;

	// ==================== State ====================

	UPROPERTY(Transient)
	TWeakObjectPtr<UStreamConfig> Config;

	UPROPERTY(Transient)
	TWeakObjectPtr<UStreamArenaConfig> ArenaConfig;

	UPROPERTY(Transient)
	TWeakObjectPtr<UStyleComponent> StyleComponent;

	UPROPERTY(Transient)
	int32 CurrentViewers = 0;

	UPROPERTY(Transient)
	int32 ViewerTarget = 0;

	UPROPERTY(Transient)
	double RunStartTimeSeconds = 0.0;

	UPROPERTY(Transient)
	float DonationRollAccumulator = 0.0f;

	UPROPERTY(Transient)
	bool bRunActive = false;

	/** Persists across runs (SaveGame integration in a later phase). */
	UPROPERTY(SaveGame)
	int64 MetaCurrency = 0;

	/** Player's stream name — used for direct mentions in chat and (later) friend voicelines. */
	UPROPERTY(SaveGame)
	FString PlayerStreamerName = TEXT("@ramless_");

	/** Owned chat broker — coordinates ambient / reactions / scripted / hints / hype / boredom / etc. */
	UPROPERTY(Transient)
	TObjectPtr<UChatBroker> ChatBroker;
};
