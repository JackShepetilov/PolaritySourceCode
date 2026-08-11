// BasketballHoop.h
// Reusable scoring volume for BasketballBall.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TimerManager.h"
#include "BasketballHoop.generated.h"

class ABasketballBall;
class AGeometryCollectionActor;
class AArenaManager;
class UBoxComponent;
class UGeometryCollection;
class UPrimitiveComponent;
class USceneComponent;
class UStaticMeshComponent;
class UNiagaraSystem;
class USoundBase;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(
	FOnBasketballHoopScored,
	class ABasketballHoop*, Hoop,
	ABasketballBall*, Ball,
	float, BallSpeed,
	FVector, ScoreLocation);

UCLASS(Blueprintable)
class POLARITY_API ABasketballHoop : public AActor
{
	GENERATED_BODY()

public:
	ABasketballHoop();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> StandFrameMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> BackboardMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> RimMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> NetMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> ScoreVolume;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> AssistVolume;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> BackboardSweetSpotVolume;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hoop|Rules")
	bool bRequireDownwardVelocity = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hoop|Rules", meta = (ClampMin = "0.0", Units = "cm/s"))
	float MinDownwardSpeed = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hoop|Rules", meta = (ClampMin = "0.0"))
	float ScoreCooldown = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hoop|Rules")
	bool bRequireChargedBall = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hoop|Rules", meta = (DeprecatedProperty, DeprecationMessage = "Combat score is retained after every goal."))
	bool bResetBallCombatScoreOnScore = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hoop|Assist")
	bool bEnableAssist = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hoop|Assist")
	bool bAssistRequiresChargedBall = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hoop|Assist")
	FVector AssistTargetLocalOffset = FVector(0.0f, 0.0f, -18.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hoop|Assist", meta = (ClampMin = "0.0", Units = "s"))
	float AssistDuration = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hoop|Assist", meta = (ClampMin = "0.0"))
	float AssistStrength = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hoop|Assist", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AssistVelocityBlend = 0.04f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hoop|Assist|Sweet Spot", meta = (ClampMin = "0.0", Units = "s"))
	float SweetSpotAssistDuration = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hoop|Assist|Sweet Spot", meta = (ClampMin = "0.0"))
	float SweetSpotAssistStrength = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hoop|Assist|Sweet Spot", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SweetSpotVelocityBlend = 0.10f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hoop|Feedback")
	TObjectPtr<UNiagaraSystem> ScoreVFX;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hoop|Feedback")
	TObjectPtr<USoundBase> ScoreSound;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hoop|Arena")
	bool bKillArenaNPCsOnScore = true;

	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Hoop|Arena", meta = (EditCondition = "bKillArenaNPCsOnScore"))
	TObjectPtr<AArenaManager> TargetArenaManager;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hoop|Arena", meta = (EditCondition = "bKillArenaNPCsOnScore"))
	bool bSequentialArenaKills = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hoop|Arena", meta = (ClampMin = "0.0", EditCondition = "bKillArenaNPCsOnScore && bSequentialArenaKills"))
	float ArenaKillDelayBetweenNPCs = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hoop|Arena", meta = (EditCondition = "bKillArenaNPCsOnScore"))
	TObjectPtr<UNiagaraSystem> ArenaKillDeathVFX;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hoop|Arena", meta = (EditCondition = "bKillArenaNPCsOnScore"))
	bool bSuppressArenaKillDrops = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hoop|Arena", meta = (EditCondition = "bKillArenaNPCsOnScore"))
	bool bGrantUpgradeAfterArenaClear = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hoop|Explosion")
	bool bEnableScoreExplosion = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hoop|Explosion", meta = (ClampMin = "0.0", Units = "cm/s", EditCondition = "bEnableScoreExplosion"))
	float SlowExplosionSpeed = 600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hoop|Explosion", meta = (ClampMin = "1.0", Units = "cm/s", EditCondition = "bEnableScoreExplosion"))
	float FullExplosionSpeed = 2600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hoop|Explosion", meta = (ClampMin = "0.0", Units = "cm", EditCondition = "bEnableScoreExplosion"))
	float SlowImpulseRadius = 350.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hoop|Explosion", meta = (ClampMin = "0.0", Units = "cm", EditCondition = "bEnableScoreExplosion"))
	float FullImpulseRadius = 1800.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hoop|Explosion", meta = (ClampMin = "0.0", EditCondition = "bEnableScoreExplosion"))
	float SlowRadialImpulse = 250.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hoop|Explosion", meta = (ClampMin = "0.0", EditCondition = "bEnableScoreExplosion"))
	float FullRadialImpulse = 3500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hoop|Explosion", meta = (EditCondition = "bEnableScoreExplosion"))
	bool bImpulseVelocityChange = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hoop|Explosion", meta = (EditCondition = "bEnableScoreExplosion"))
	TEnumAsByte<ERadialImpulseFalloff> ImpulseFalloff = ERadialImpulseFalloff::RIF_Linear;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hoop|Explosion|Feedback", meta = (EditCondition = "bEnableScoreExplosion"))
	TObjectPtr<UNiagaraSystem> ExplosionVFX;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hoop|Explosion|Feedback", meta = (ClampMin = "0.0", EditCondition = "bEnableScoreExplosion"))
	float SlowExplosionVFXScale = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hoop|Explosion|Feedback", meta = (ClampMin = "0.0", EditCondition = "bEnableScoreExplosion"))
	float FullExplosionVFXScale = 2.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hoop|Explosion|Feedback", meta = (EditCondition = "bEnableScoreExplosion"))
	TObjectPtr<USoundBase> ExplosionSound;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hoop|Explosion|Feedback", meta = (ClampMin = "0.0", EditCondition = "bEnableScoreExplosion"))
	float SlowExplosionSoundVolume = 0.65f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hoop|Explosion|Feedback", meta = (ClampMin = "0.0", EditCondition = "bEnableScoreExplosion"))
	float FullExplosionSoundVolume = 1.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hoop|Explosion|Feedback", meta = (ClampMin = "0.1", ClampMax = "3.0", EditCondition = "bEnableScoreExplosion"))
	float SlowExplosionSoundPitch = 0.9f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hoop|Explosion|Feedback", meta = (ClampMin = "0.1", ClampMax = "3.0", EditCondition = "bEnableScoreExplosion"))
	float FullExplosionSoundPitch = 1.12f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hoop|Explosion|Geometry", meta = (EditCondition = "bEnableScoreExplosion"))
	TObjectPtr<UGeometryCollection> StandFrameGeometryCollection;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hoop|Explosion|Geometry", meta = (EditCondition = "bEnableScoreExplosion"))
	TObjectPtr<UGeometryCollection> BackboardGeometryCollection;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hoop|Explosion|Geometry", meta = (EditCondition = "bEnableScoreExplosion"))
	TObjectPtr<UGeometryCollection> RimGeometryCollection;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hoop|Explosion|Geometry", meta = (EditCondition = "bEnableScoreExplosion"))
	TObjectPtr<UGeometryCollection> NetGeometryCollection;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hoop|Explosion|Geometry", meta = (EditCondition = "bEnableScoreExplosion"))
	FName GCGibCollisionProfile = FName("Ragdoll");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hoop|Explosion|Geometry", meta = (ClampMin = "0.0", EditCondition = "bEnableScoreExplosion"))
	float GCExternalStrain = 999999.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hoop|Explosion|Geometry", meta = (ClampMin = "0.0", EditCondition = "bEnableScoreExplosion"))
	float GCGibLifetime = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hoop|Explosion|Geometry", meta = (ClampMin = "0.0", EditCondition = "bEnableScoreExplosion"))
	float GCGibFreezeTime = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hoop|Debug")
	bool bLogScore = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hoop|Debug")
	bool bLogAssist = false;

	UPROPERTY(BlueprintReadOnly, Category = "Hoop|State")
	int32 ScoreCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Hoop|State")
	float LastBallSpeed = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Hoop|State")
	FVector LastScoreLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Hoop|State")
	bool bHasExploded = false;

	UPROPERTY(BlueprintAssignable, Category = "Hoop|Events")
	FOnBasketballHoopScored OnBasketballHoopScored;

	UFUNCTION(BlueprintCallable, Category = "Hoop")
	bool TriggerScore(ABasketballBall* Ball);

protected:
	UFUNCTION()
	void OnScoreVolumeBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnAssistVolumeBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnSweetSpotVolumeBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	void TryStartAssistForActor(AActor* OtherActor, bool bSweetSpot);
	FVector GetAssistTargetLocation() const;
	void PlayScoreFeedback(const FVector& Location);
	void TriggerArenaCompletion();
	AArenaManager* ResolveArenaManager() const;
	float CalculateExplosionPower(float BallSpeed) const;
	void TriggerScoreExplosion(const FVector& Origin, float ExplosionPower);
	void PlayExplosionFeedback(const FVector& Origin, float ExplosionPower) const;
	void SpawnAndBreakGeometryCollection(UStaticMeshComponent* SourceMesh, UGeometryCollection* GeometryCollection);
	void FreezeSpawnedGeometryCollections();

	float LastScoreTime = -1000.0f;

	UPROPERTY()
	TArray<TObjectPtr<AGeometryCollectionActor>> SpawnedGeometryCollectionActors;

	FTimerHandle GCImpulseHandle;
	FTimerHandle GCFreezeHandle;
};
