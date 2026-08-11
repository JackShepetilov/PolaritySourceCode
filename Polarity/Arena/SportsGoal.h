// SportsGoal.h
// One-shot football-style arena goal. Any ball entry scores; ball speed only scales the payoff.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TimerManager.h"
#include "SportsGoal.generated.h"

class AGeometryCollectionActor;
class AArenaManager;
class ASportsGoal;
class ASportsBall;
class UBoxComponent;
class UCurveFloat;
class UGeometryCollection;
class UStaticMeshComponent;
class USceneComponent;
class UPrimitiveComponent;
class UNiagaraSystem;
class USoundBase;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FiveParams(
	FOnSportsGoalScored,
	ASportsGoal*, Goal,
	ASportsBall*, Ball,
	float, BallSpeed,
	float, GoalPower,
	FVector, ExplosionOrigin);

UCLASS(Blueprintable)
class POLARITY_API ASportsGoal : public AActor
{
	GENERATED_BODY()

public:
	ASportsGoal();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> GoalVolume;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> FrameMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> NetMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> ExplosionOrigin;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Goal|Rules")
	bool bOneShot = true;

	UPROPERTY(BlueprintReadOnly, Category = "Goal|Rules")
	bool bHasScored = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Goal|Power", meta = (ClampMin = "1.0", Units = "cm/s"))
	float FullPowerSpeed = 3000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Goal|Power", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SlowGoalPower = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Goal|Power")
	TObjectPtr<UCurveFloat> SpeedToPowerCurve;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Goal|Explosion", meta = (ClampMin = "0.0", Units = "cm"))
	float SlowImpulseRadius = 350.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Goal|Explosion", meta = (ClampMin = "0.0", Units = "cm"))
	float FullImpulseRadius = 1800.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Goal|Explosion", meta = (ClampMin = "0.0"))
	float SlowRadialImpulse = 250.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Goal|Explosion", meta = (ClampMin = "0.0"))
	float FullRadialImpulse = 3500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Goal|Explosion")
	bool bImpulseVelocityChange = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Goal|Explosion")
	TEnumAsByte<ERadialImpulseFalloff> ImpulseFalloff = ERadialImpulseFalloff::RIF_Linear;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Goal|Feedback")
	TObjectPtr<UNiagaraSystem> GoalVFX;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Goal|Feedback", meta = (ClampMin = "0.0"))
	float SlowVFXScale = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Goal|Feedback", meta = (ClampMin = "0.0"))
	float FullVFXScale = 2.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Goal|Feedback")
	TObjectPtr<USoundBase> GoalSound;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Goal|Feedback", meta = (ClampMin = "0.0"))
	float SlowSoundVolume = 0.65f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Goal|Feedback", meta = (ClampMin = "0.0"))
	float FullSoundVolume = 1.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Goal|Feedback", meta = (ClampMin = "0.1", ClampMax = "3.0"))
	float SlowSoundPitch = 0.9f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Goal|Feedback", meta = (ClampMin = "0.1", ClampMax = "3.0"))
	float FullSoundPitch = 1.12f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Goal|Ball")
	bool bHideBallOnGoal = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Goal|Ball")
	bool bDisableBallCollisionOnGoal = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Goal|Arena")
	bool bKillArenaNPCsOnGoal = true;

	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Goal|Arena", meta = (EditCondition = "bKillArenaNPCsOnGoal"))
	TObjectPtr<AArenaManager> TargetArenaManager;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Goal|Arena", meta = (EditCondition = "bKillArenaNPCsOnGoal"))
	bool bSequentialArenaKills = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Goal|Arena", meta = (ClampMin = "0.0", EditCondition = "bKillArenaNPCsOnGoal && bSequentialArenaKills"))
	float ArenaKillDelayBetweenNPCs = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Goal|Arena", meta = (EditCondition = "bKillArenaNPCsOnGoal"))
	TObjectPtr<UNiagaraSystem> ArenaKillDeathVFX;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Goal|Arena", meta = (EditCondition = "bKillArenaNPCsOnGoal"))
	bool bSuppressArenaKillDrops = true;

	/** Grant one upgrade after the final goal-triggered NPC death and arena completion. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Goal|Arena", meta = (EditCondition = "bKillArenaNPCsOnGoal"))
	bool bGrantUpgradeAfterArenaClear = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Goal|Net")
	bool bHideNetOnGoal = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Goal|Net")
	bool bDisableNetCollisionOnGoal = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Goal|Frame")
	bool bHideFrameMeshWhenGCSpawns = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Goal|Frame")
	TObjectPtr<UGeometryCollection> FrameGeometryCollection;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Goal|Frame")
	FName FrameGibCollisionProfile = FName("Ragdoll");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Goal|Frame", meta = (ClampMin = "0.0"))
	float GCExternalStrain = 999999.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Goal|Frame", meta = (ClampMin = "0.0"))
	float GCGibLifetime = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Goal|Frame", meta = (ClampMin = "0.0"))
	float GCGibFreezeTime = 3.0f;

	/** Optional extra physics pieces in the Blueprint goal, useful before the real GC asset exists. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Goal|Frame")
	TArray<TObjectPtr<UPrimitiveComponent>> ExtraImpulsePieces;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Goal|Debug")
	bool bLogGoal = true;

	UPROPERTY(BlueprintReadOnly, Category = "Goal|State")
	float LastBallSpeed = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Goal|State")
	float LastGoalPower = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Goal|State")
	FVector LastExplosionOrigin = FVector::ZeroVector;

	UPROPERTY(BlueprintAssignable, Category = "Goal|Events")
	FOnSportsGoalScored OnSportsGoalScored;

	UFUNCTION(BlueprintCallable, Category = "Goal")
	bool TriggerGoal(ASportsBall* Ball);

	UFUNCTION(BlueprintPure, Category = "Goal")
	float CalculateGoalPower(float BallSpeed) const;

protected:
	UFUNCTION()
	void OnGoalVolumeBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	void HideNet();
	void ResolveBallAfterGoal(ASportsBall* Ball);
	void PlayGoalFeedback(const FVector& Origin, float GoalPower);
	void ApplyExplosionImpulse(const FVector& Origin, float GoalPower);
	void SpawnAndBreakFrameGC(const FVector& Origin, float GoalPower);
	void TriggerArenaManagerKillEvent();
	AArenaManager* ResolveArenaManager() const;
	void FreezeSpawnedGC();

	UPROPERTY()
	TObjectPtr<AGeometryCollectionActor> SpawnedFrameGCActor;

	FTimerHandle GCImpulseHandle;
	FTimerHandle GCFreezeHandle;
};
