// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PlayerDeathSequenceComponent.generated.h"

class ACameraActor;
class AShooterCharacter;
class AShooterNPC;
class UAnimMontage;
class UAudioComponent;
class UCameraShakeBase;
class UCurveFloat;
class UGeometryCollection;
class UNiagaraComponent;
class UNiagaraSystem;
class USoundBase;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnPlayerDeathSequenceEvent);

/**
 * Configurable terminal player-death presentation.
 *
 * The component owns a transient camera, gathers every AShooterNPC subclass inside the
 * configured radius, pulls them into a ring around the player using their capture montage,
 * then dismembers the player and all gathered NPCs on the same frame.
 *
 * No content references are hard-coded. Assign montages, GC, VFX and SFX on the player BP.
 */
UCLASS(ClassGroup = (Cinematics), meta = (BlueprintSpawnableComponent))
class POLARITY_API UPlayerDeathSequenceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPlayerDeathSequenceComponent();

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	/** Starts the sequence. Returns false when disabled, already running, or ownership is invalid. */
	UFUNCTION(BlueprintCallable, Category = "Death Sequence")
	bool StartDeathSequence();

	UFUNCTION(BlueprintPure, Category = "Death Sequence")
	bool IsSequenceActive() const { return bSequenceActive; }

	/** Delay from sequence start until the screen is fully black. */
	UFUNCTION(BlueprintPure, Category = "Death Sequence")
	float GetTotalDuration() const;

	// ==================== Master / Timing ====================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death Sequence")
	bool bEnabled = true;

	/** Time from sequence start to the synchronized explosion. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death Sequence|Timing",
		meta = (ClampMin = "0.1", ClampMax = "10.0", Units = "s"))
	float ExplosionDelay = 1.5f;

	/** Time to observe the explosion before fading out. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death Sequence|Timing",
		meta = (ClampMin = "0.0", ClampMax = "10.0", Units = "s"))
	float PostExplosionHoldDuration = 0.65f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death Sequence|Timing",
		meta = (ClampMin = "0.05", ClampMax = "5.0", Units = "s"))
	float FadeDuration = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death Sequence|Timing")
	FLinearColor FadeColor = FLinearColor::Black;

	// ==================== Player Presentation ====================

	/** Montage played on the player's third-person/world mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death Sequence|Player")
	TObjectPtr<UAnimMontage> PlayerDeathMontage;

	/** Geometry Collection used for the player's body explosion. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death Sequence|Player")
	TObjectPtr<UGeometryCollection> PlayerDeathGeometryCollection;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death Sequence|Player",
		meta = (ClampMin = "0.0"))
	float PlayerDismembermentImpulse = 950.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death Sequence|Player",
		meta = (ClampMin = "0.0"))
	float PlayerDismembermentAngularImpulse = 140.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death Sequence|Player",
		meta = (ClampMin = "0.1", ClampMax = "30.0", Units = "s"))
	float PlayerGibLifetime = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death Sequence|Player")
	FName GibCollisionProfile = TEXT("Ragdoll");

	// ==================== Enemy Pull ====================

	/** All AShooterNPC subclasses inside this radius are gathered. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death Sequence|Enemy Pull",
		meta = (ClampMin = "0.0", Units = "cm"))
	float EnemyCaptureRadius = 1800.0f;

	/** 0 means unlimited. Nearest enemies are preferred when a limit is set. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death Sequence|Enemy Pull",
		meta = (ClampMin = "0"))
	int32 MaximumCapturedEnemies = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death Sequence|Enemy Pull",
		meta = (ClampMin = "0.0", ClampMax = "10.0", Units = "s"))
	float EnemyPullStartDelay = 0.1f;

	/** Optional normalized 0..1 pull curve. SmoothStep is used when unset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death Sequence|Enemy Pull")
	TObjectPtr<UCurveFloat> EnemyPullCurve;

	/** Final horizontal distance from the player, preventing all enemies/GCs occupying one point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death Sequence|Enemy Pull",
		meta = (ClampMin = "0.0", Units = "cm"))
	float EnemyHoldRadius = 135.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death Sequence|Enemy Pull",
		meta = (Units = "cm"))
	float EnemyHoldHeightOffset = 35.0f;

	/** Optional shared override; null keeps each enemy's own CapturedMontage. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death Sequence|Enemy Pull")
	TObjectPtr<UAnimMontage> EnemyCaptureMontageOverride;

	/** Multiplies each NPC's configured dismemberment velocities for this synchronized death. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death Sequence|Enemy Pull",
		meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float EnemyDismembermentImpulseMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death Sequence|Enemy Pull")
	bool bFacePlayerDuringPull = true;

	// ==================== Camera ====================

	/** Local-space offset from the active first-person camera at sequence start. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death Sequence|Camera")
	FVector CameraStartLocalOffset = FVector::ZeroVector;

	/** Initial local-space velocity: X forward, Y right, Z up. Default is up and backward. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death Sequence|Camera",
		meta = (Units = "cm/s"))
	FVector CameraInitialVelocity = FVector(-850.0f, 0.0f, 720.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death Sequence|Camera",
		meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float CameraGravityScale = 0.65f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death Sequence|Camera",
		meta = (ClampMin = "0.0", ClampMax = "20.0"))
	float CameraLinearDrag = 0.85f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death Sequence|Camera",
		meta = (ClampMin = "0.0", ClampMax = "100.0", Units = "cm"))
	float CameraCollisionRadius = 18.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death Sequence|Camera",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CameraRestitution = 0.22f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death Sequence|Camera",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CameraSurfaceFriction = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death Sequence|Camera",
		meta = (ClampMin = "0.0", ClampMax = "50.0"))
	float CameraLookAtInterpSpeed = 9.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death Sequence|Camera",
		meta = (Units = "cm"))
	float CameraFocusHeightOffset = 70.0f;

	/** Initial roll angular velocity; gives the launch some physical tumbling. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death Sequence|Camera",
		meta = (Units = "deg/s"))
	float CameraRollVelocity = 32.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death Sequence|Camera",
		meta = (ClampMin = "0.0", ClampMax = "20.0"))
	float CameraAngularDrag = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death Sequence|Camera",
		meta = (ClampMin = "0.0", ClampMax = "2.0", Units = "s"))
	float CameraBlendInDuration = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death Sequence|Camera",
		meta = (ClampMin = "20.0", ClampMax = "170.0", Units = "deg"))
	float CameraMinimumFOV = 72.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death Sequence|Camera",
		meta = (ClampMin = "20.0", ClampMax = "170.0", Units = "deg"))
	float CameraMaximumFOV = 108.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death Sequence|Camera",
		meta = (ClampMin = "0.0", ClampMax = "90.0", Units = "deg"))
	float CameraCompositionFOVPadding = 18.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death Sequence|Camera",
		meta = (ClampMin = "0.0", ClampMax = "50.0"))
	float CameraFOVInterpSpeed = 7.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death Sequence|Camera")
	TSubclassOf<UCameraShakeBase> ExplosionCameraShake;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death Sequence|Camera",
		meta = (ClampMin = "0.0"))
	float ExplosionCameraShakeScale = 1.0f;

	// ==================== VFX / SFX ====================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death Sequence|VFX")
	TObjectPtr<UNiagaraSystem> WindupVFX;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death Sequence|VFX")
	TObjectPtr<UNiagaraSystem> EnemyCaptureVFX;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death Sequence|VFX")
	TObjectPtr<UNiagaraSystem> PlayerExplosionVFX;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death Sequence|VFX")
	TObjectPtr<UNiagaraSystem> EnemyExplosionVFX;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death Sequence|SFX")
	TObjectPtr<USoundBase> WindupSound;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death Sequence|SFX")
	TObjectPtr<USoundBase> ExplosionSound;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death Sequence|SFX")
	TObjectPtr<USoundBase> EnemyExplosionSound;

	// ==================== Blueprint Hooks ====================

	UPROPERTY(BlueprintAssignable, Category = "Death Sequence|Events")
	FOnPlayerDeathSequenceEvent OnSequenceStarted;

	UPROPERTY(BlueprintAssignable, Category = "Death Sequence|Events")
	FOnPlayerDeathSequenceEvent OnSynchronizedExplosion;

	UPROPERTY(BlueprintAssignable, Category = "Death Sequence|Events")
	FOnPlayerDeathSequenceEvent OnFadeStarted;

private:
	struct FEnemyTarget
	{
		TWeakObjectPtr<AShooterNPC> NPC;
		FVector StartLocation = FVector::ZeroVector;
		FVector HoldDirection = FVector::ForwardVector;
		float StartDistanceSq = 0.0f;
	};

	TWeakObjectPtr<AShooterCharacter> OwnerCharacter;
	TObjectPtr<ACameraActor> DeathCamera;
	TObjectPtr<UNiagaraComponent> ActiveWindupVFX;
	TObjectPtr<UAudioComponent> ActiveWindupAudio;
	TArray<FEnemyTarget> CapturedEnemies;

	FVector CameraVelocity = FVector::ZeroVector;
	float CurrentCameraRoll = 0.0f;
	float CurrentCameraRollVelocity = 0.0f;
	float ElapsedTime = 0.0f;
	bool bSequenceActive = false;
	bool bExplosionTriggered = false;
	bool bFadeTriggered = false;

	void GatherEnemies();
	void UpdateEnemyPull(float DeltaTime);
	void SpawnDeathCamera();
	void UpdateDeathCamera(float DeltaTime);
	FVector CalculateCompositionFocus(float& OutBoundingRadius) const;
	void TriggerSynchronizedExplosion();
	void SpawnPlayerGeometryCollection();
	void StartFade();
	void CleanupTransientEffects();
};
