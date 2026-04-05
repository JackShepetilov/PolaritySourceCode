// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ArenaFinaleSequence.generated.h"

class AArenaManager;
class AShooterNPC;
class AShooterCharacter;
class UChargeAnimationComponent;
class UCurveFloat;
class UNiagaraSystem;
class UInputAction;
class UEnhancedInputComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnFinaleStarted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnFinalePlayerActivated);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnFinaleCompleted);

/**
 * Cinematic finale sequence for arena completion.
 *
 * When triggered (call StartFinaleSequence), prompts the player to press the grab button.
 * On press: plays a custom montage, locks camera, stuns all NPCs, then kills them
 * one-by-one in random order with special VFX. All timing uses real (unscaled) time
 * for compatibility with global time dilation.
 *
 * Place one per arena level. Configure via Details panel.
 */
UCLASS(Blueprintable)
class POLARITY_API AArenaFinaleSequence : public AActor
{
	GENERATED_BODY()

public:
	AArenaFinaleSequence();

	// ==================== Animation ====================

	/** Montage to play on the player when they press the grab button */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Finale|Animation")
	TObjectPtr<UAnimMontage> FinaleAnimMontage;

	/** Curve controlling montage play rate over normalized time (0→1). If null, uses constant BasePlayRate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Finale|Animation")
	TObjectPtr<UCurveFloat> FinalePlayRateCurve;

	/** Base multiplier for montage play rate */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Finale|Animation", meta = (ClampMin = "0.01"))
	float FinaleAnimBasePlayRate = 1.0f;

	// ==================== NPC ====================

	/** Anim montage for stunned NPCs during the sequence */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Finale|NPC")
	TObjectPtr<UAnimMontage> NPCStunMontage;

	/** How long NPCs stay stunned (should exceed total sequence duration) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Finale|NPC", meta = (ClampMin = "0.1"))
	float NPCStunDuration = 10.0f;

	/** Real-time delay after stun before kills begin */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Finale|NPC", meta = (ClampMin = "0.0"))
	float DelayBeforeKills = 1.5f;

	/** Real-time delay between each NPC death */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Finale|NPC", meta = (ClampMin = "0.01"))
	float DelayBetweenKills = 0.3f;

	/** VFX spawned at each NPC's location on finale death */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Finale|NPC")
	TObjectPtr<UNiagaraSystem> FinaleDeathVFX;

	/** Scale for the death VFX */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Finale|NPC")
	FVector FinaleDeathVFXScale = FVector(1.0f);

	// ==================== Camera ====================

	/** Actor whose location the camera locks onto during the sequence */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Finale|Camera")
	TSoftObjectPtr<AActor> CameraLockTarget;

	/** Camera rotation blend speed (seconds to reach target) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Finale|Camera", meta = (ClampMin = "0.01"))
	float CameraBlendTime = 0.3f;

	// ==================== References ====================

	/** Arena to pull alive NPCs from */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Finale|References")
	TSoftObjectPtr<AArenaManager> LinkedArena;

	// ==================== Events ====================

	/** Fired when sequence starts — use to show UI prompt */
	UPROPERTY(BlueprintAssignable, Category = "Finale|Events")
	FOnFinaleStarted OnFinaleStarted;

	/** Fired when player presses the grab button */
	UPROPERTY(BlueprintAssignable, Category = "Finale|Events")
	FOnFinalePlayerActivated OnFinalePlayerActivated;

	/** Fired when all NPCs are dead and sequence is complete */
	UPROPERTY(BlueprintAssignable, Category = "Finale|Events")
	FOnFinaleCompleted OnFinaleCompleted;

	// ==================== API ====================

	/** Start the finale sequence. Call from OnPropPercentChanged binding or any other trigger. */
	UFUNCTION(BlueprintCallable, Category = "Finale")
	void StartFinaleSequence();

protected:
	virtual void Tick(float DeltaTime) override;

private:

	// ==================== Input ====================

	/** Called when player presses the grab/charge button during the prompt phase */
	void OnPlayerActivated();

	/** Input binding handles (for cleanup) */
	TArray<uint32> InputBindingHandles;

	/** Bind to player's grab input actions */
	void BindPlayerInput();

	/** Unbind from player's grab input actions */
	void UnbindPlayerInput();

	/** Enhanced Input callback */
	void OnGrabInputTriggered();

	// ==================== Camera ====================

	/** Update camera rotation toward lock target (using real-time delta) */
	void UpdateCameraLock(float RealDelta);

	/** Resolved camera lock world position */
	FVector ResolvedCameraTarget = FVector::ZeroVector;

	/** Whether camera is currently locked */
	bool bCameraLocked = false;

	// ==================== Montage ====================

	/** Update montage play rate from curve (compensating for time dilation) */
	void UpdateFinalePlayRate(float RealDelta);

	/** Elapsed real time since montage started */
	float MontageRealTimeElapsed = 0.0f;

	/** Total montage duration (real-time, from montage asset) */
	float MontageTotalDuration = 0.0f;

	/** Whether montage is actively playing */
	bool bMontageActive = false;

	// ==================== Kill Phase ====================

	/** Kill the next NPC in the shuffled queue */
	void KillNextNPC();

	/** Perform finale death on a single NPC */
	void FinaleKillNPC(AShooterNPC* NPC);

	/** End the entire sequence — restore input, camera, broadcast completion */
	void EndFinaleSequence();

	/** Shuffled queue of NPCs to kill */
	UPROPERTY()
	TArray<TWeakObjectPtr<AShooterNPC>> PendingKillNPCs;

	/** Real-time timestamp for next kill */
	double NextKillRealTime = 0.0;

	/** Whether we're in the kill phase (past DelayBeforeKills) */
	bool bInKillPhase = false;

	// ==================== State ====================

	/** Whether the sequence is currently active */
	bool bSequenceActive = false;

	/** Whether we're waiting for player input (prompt phase) */
	bool bWaitingForInput = false;

	// ==================== Cached References ====================

	UPROPERTY()
	TWeakObjectPtr<AShooterCharacter> CachedPlayer;

	UPROPERTY()
	TWeakObjectPtr<UChargeAnimationComponent> CachedChargeComp;

	UPROPERTY()
	TWeakObjectPtr<APlayerController> CachedPC;
};
