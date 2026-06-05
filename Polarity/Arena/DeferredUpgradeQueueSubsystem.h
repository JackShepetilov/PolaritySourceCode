// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "DeferredUpgradeQueueSubsystem.generated.h"

class UXPSubsystem;

USTRUCT(BlueprintType)
struct FDeferredLevelUp
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Deferred Upgrade")
	int32 NewLevel = 0;
};

/**
 * Broadcast when FlushAll() releases one queued level-up — UI listens to this and shows the
 * upgrade-choice popup like a normal level-up. Bind UUpgradeChoiceWidget (or a thin BP wrapper)
 * to this delegate so the existing popup queue inside the widget handles ordering for free.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDeferredLevelUpReleased, int32, NewLevel);

/**
 * Captures level-ups that happen during gated periods (e.g. while an arena is active) and
 * releases them later in a controlled flush — typically when the player activates an antenna
 * and uploads data.
 *
 * Wiring:
 *   - Subsystem listens to UXPSubsystem::OnLevelUp at all times (passive).
 *   - When NOT capturing, OnLevelUp passes straight through to OnDeferredLevelUpReleased
 *     so UI behavior is unchanged from the player's perspective.
 *   - When capturing (BeginCapture → EndCapture / FlushAll), level-ups are stashed in the
 *     PendingLevelUps queue instead of releasing immediately.
 *   - FlushAll() drains the queue and broadcasts OnDeferredLevelUpReleased once per entry.
 *
 * UI integration:
 *   UUpgradeChoiceWidget subscribes to OnDeferredLevelUpReleased (with a fallback to the raw
 *   XPSubsystem::OnLevelUp when this subsystem is unavailable). See UpgradeChoiceWidget.cpp.
 */
UCLASS()
class POLARITY_API UDeferredUpgradeQueueSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ==================== API ====================

	/** Start intercepting level-ups. While capturing, level-ups go into the queue
	 *  and OnDeferredLevelUpReleased does NOT fire until FlushAll(). */
	UFUNCTION(BlueprintCallable, Category = "Deferred Upgrade")
	void BeginCapture();

	/** Stop capturing without flushing — leaves the queue intact for a later FlushAll(). */
	UFUNCTION(BlueprintCallable, Category = "Deferred Upgrade")
	void EndCapture();

	/** Release every queued level-up in order, broadcasting OnDeferredLevelUpReleased
	 *  once per entry. Also clears the queue and stops capturing. */
	UFUNCTION(BlueprintCallable, Category = "Deferred Upgrade")
	void FlushAll();

	/** Throw away every queued level-up without firing the release delegate.
	 *  Used when the player dies or the arena resets — pending choices are gone. */
	UFUNCTION(BlueprintCallable, Category = "Deferred Upgrade")
	void ClearWithoutReleasing();

	/** True between BeginCapture and EndCapture/FlushAll. */
	UFUNCTION(BlueprintPure, Category = "Deferred Upgrade")
	bool IsCapturing() const { return bCapturing; }

	UFUNCTION(BlueprintPure, Category = "Deferred Upgrade")
	int32 GetPendingCount() const { return PendingLevelUps.Num(); }

	// ==================== Events ====================

	/** Fires once per level-up: either straight through (not capturing) or one-by-one during flush. */
	UPROPERTY(BlueprintAssignable, Category = "Deferred Upgrade")
	FOnDeferredLevelUpReleased OnDeferredLevelUpReleased;

private:
	UFUNCTION()
	void HandleLevelUp(int32 NewLevel);

	UPROPERTY()
	TArray<FDeferredLevelUp> PendingLevelUps;

	bool bCapturing = false;

	UPROPERTY()
	TWeakObjectPtr<UXPSubsystem> CachedXPSubsystem;
};
