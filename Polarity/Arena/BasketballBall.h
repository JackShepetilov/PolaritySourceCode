// BasketballBall.h
// Capture-only sports ball. Ignores melee and launches from capture release.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UObject/ObjectKey.h"
#include "BasketballBall.generated.h"

class AEMFChannelingPlateActor;
class AShooterCharacter;
class AShooterNPC;
class UPhysicalMaterial;
class UPrimitiveComponent;
class UStaticMeshComponent;
class UDamageType;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(
	FOnBasketballBallLaunched,
	class ABasketballBall*, Ball,
	AActor*, Thrower,
	float, HoldTime,
	FVector, LaunchVelocity);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FOnBasketballCombatScoreChanged,
	int32, CurrentScore,
	int32, RequiredScore,
	float, NormalizedScore);

UCLASS(Blueprintable)
class POLARITY_API ABasketballBall : public AActor
{
	GENERATED_BODY()

public:
	ABasketballBall();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> BallMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Physics", meta = (ClampMin = "10.0", Units = "cm"))
	float BallDiameter = 55.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Physics", meta = (ClampMin = "0.1", Units = "kg"))
	float BallMassKg = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Physics", meta = (ClampMin = "0.0"))
	float LinearDamping = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Physics", meta = (ClampMin = "0.0"))
	float AngularDamping = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Physics")
	bool bUseCCD = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Physics")
	TObjectPtr<UPhysicalMaterial> BallPhysicalMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Capture")
	bool bCanBeCaptured = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Capture", meta = (ClampMin = "50.0", Units = "cm"))
	float CaptureRange = 1200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Capture", meta = (ClampMin = "1.0"))
	float CaptureFollowInterpSpeed = 18.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Capture", meta = (ClampMin = "0.0"))
	float CaptureVelocityDamping = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Capture")
	FVector CaptureTargetLocalOffset = FVector(0.0f, 35.0f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Launch", meta = (ClampMin = "0.0", Units = "s"))
	float MaxThrowChargeTime = 1.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Launch", meta = (ClampMin = "0.0", Units = "cm/s"))
	float MinLaunchSpeed = 900.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Launch", meta = (ClampMin = "0.0", Units = "cm/s"))
	float MaxLaunchSpeed = 2600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Launch", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LaunchUpwardBias = 0.22f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Launch", meta = (ClampMin = "0.0"))
	float LaunchSpinVelocityChange = 900.0f;

	// ==================== Return Bounce ====================
	// Mirrors Air Mail's return rules, but remains self-contained: the ball never receives
	// AirMailIncoming, so it cannot be redirected by melee.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Return Bounce")
	bool bEnableReturnBounce = true;

	/** Minimum impact angle to the surface plane. 90 = straight into the surface. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Return Bounce", meta = (ClampMin = "0.0", ClampMax = "90.0", Units = "deg"))
	float MinBounceAngleDeg = 60.0f;

	/** Minimum pre-impact speed for the ball to return. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Return Bounce", meta = (ClampMin = "0.0", Units = "cm/s"))
	float MinBounceImpactSpeed = 400.0f;

	/** Flight speed of the returning ball. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Return Bounce", meta = (ClampMin = "100.0", Units = "cm/s"))
	float ReturnSpeed = 1100.0f;

	/** Height above the thrower's camera used as the return target. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Return Bounce", meta = (Units = "cm"))
	float ReturnTargetHeightOffset = 25.0f;

	/** Angular speed applied when the ball starts returning. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Return Bounce", meta = (ClampMin = "0.0", Units = "deg/s"))
	float ReturnSpinSpeed = 360.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Hoop Assist", meta = (ClampMin = "0.0", Units = "cm/s"))
	float MinAssistSpeed = 200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Hoop Assist", meta = (ClampMin = "0.0", Units = "cm/s"))
	float MaxAssistSpeed = 3600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Combat", meta = (ClampMin = "0.0", Units = "cm/s"))
	float MinDamageImpactSpeed = 700.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Combat", meta = (ClampMin = "0.0", Units = "cm/s"))
	float FullDamageImpactSpeed = 2600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Combat", meta = (ClampMin = "0.0"))
	float MinImpactDamage = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Combat", meta = (ClampMin = "0.0"))
	float FullImpactDamage = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Combat", meta = (ClampMin = "0.0", Units = "s"))
	float MinImpactStunDuration = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Combat", meta = (ClampMin = "0.0", Units = "s"))
	float FullImpactStunDuration = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Combat", meta = (ClampMin = "0.0", Units = "s"))
	float PerTargetHitCooldown = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Combat")
	TSubclassOf<UDamageType> ImpactDamageType;

	/** Player healing per point of damage accepted by the enemy. 0.25 = 25% lifesteal. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Combat|Healing", meta = (ClampMin = "0.0"))
	float HealFractionOfDamageDealt = 0.25f;

	/** Required hit/kill score before the hoop accepts the ball. 0 = ready immediately. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Combat Score", meta = (ClampMin = "0"))
	int32 RequiredCombatScore = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Combat Score", meta = (ClampMin = "0"))
	int32 CombatScoreOnKill = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Combat Score")
	bool bAwardCombatScoreOnNonLethalHit = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Combat Score", meta = (ClampMin = "0"))
	int32 CombatScoreOnNonLethalHit = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Debug")
	bool bLogCapture = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Debug")
	bool bLogCombatHits = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Debug")
	bool bLogReturnBounces = false;

	UPROPERTY(BlueprintReadOnly, Category = "Ball|State")
	bool bCaptured = false;

	UPROPERTY(BlueprintReadOnly, Category = "Ball|State")
	float LastThrowHoldTime = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Ball|State")
	FVector LastLaunchVelocity = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Ball|State")
	FVector LastPreImpactVelocity = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Ball|State")
	float LastImpactSpeed = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Ball|Combat Score")
	int32 CurrentCombatScore = 0;

	UPROPERTY(BlueprintAssignable, Category = "Ball|Events")
	FOnBasketballBallLaunched OnBasketballBallLaunched;

	UPROPERTY(BlueprintAssignable, Category = "Ball|Events")
	FOnBasketballCombatScoreChanged OnBasketballCombatScoreChanged;

	UPROPERTY(BlueprintAssignable, Category = "Ball|Events")
	FOnBasketballCombatScoreChanged OnBasketballCombatScoreAdded;

	UFUNCTION(BlueprintPure, Category = "Ball|Capture")
	bool IsCapturedByPlate() const { return CapturingPlate.IsValid(); }

	UFUNCTION(BlueprintCallable, Category = "Ball|Capture")
	void SetCapturedByPlate(AEMFChannelingPlateActor* Plate);

	UFUNCTION(BlueprintCallable, Category = "Ball|Capture")
	void ReleasedFromCapture();

	UFUNCTION(BlueprintCallable, Category = "Ball|Launch")
	void LaunchBall(const FVector& AimDirection, float HoldTime, AActor* Thrower);

	UFUNCTION(BlueprintPure, Category = "Ball|Launch")
	float CalculateLaunchSpeed(float HoldTime) const;

	UFUNCTION(BlueprintCallable, Category = "Ball|Hoop Assist")
	void StartHoopAssist(const FVector& TargetLocation, float Duration, float Strength, float VelocityBlend);

	UFUNCTION(BlueprintPure, Category = "Ball|Combat Score")
	bool IsCombatScoreReady() const;

	UFUNCTION(BlueprintPure, Category = "Ball|Combat Score")
	float GetCombatScoreNormalized() const;

	UFUNCTION(BlueprintCallable, Category = "Ball|Combat Score")
	void AddCombatScore(int32 Amount);

	UFUNCTION(BlueprintCallable, Category = "Ball|Combat Score")
	void ResetCombatScore();

protected:
	void ApplyBallSettings();
	FVector GetCaptureTargetLocation(const AEMFChannelingPlateActor* Plate) const;
	FVector BuildLaunchDirection(const FVector& AimDirection) const;
	bool TryReturnBounce(AActor* OtherActor, const FHitResult& Hit, const FVector& PreImpactVelocity);
	bool QualifiesForReturnBounce(const FVector& PreImpactVelocity, const FVector& ImpactNormal, bool bCharacterImpact) const;
	bool ComputeReturnBounceVelocity(FVector& OutVelocity) const;
	AShooterCharacter* GetReturnTargetCharacter() const;
	void UpdateHoopAssist(float DeltaTime);
	float CalculateImpactDamage(float ImpactSpeed) const;
	float CalculateImpactStunDuration(float ImpactSpeed) const;
	void AwardCombatScoreForHit(AShooterNPC* HitNPC, bool bKilled);

	UFUNCTION()
	void OnBallHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		FVector NormalImpulse, const FHitResult& Hit);

	UPROPERTY()
	TWeakObjectPtr<AEMFChannelingPlateActor> CapturingPlate;

	bool bHoopAssistActive = false;
	FVector HoopAssistTarget = FVector::ZeroVector;
	float HoopAssistTimeRemaining = 0.0f;
	float HoopAssistStrength = 0.0f;
	float HoopAssistVelocityBlend = 0.0f;

	FVector CachedPhysicsVelocity = FVector::ZeroVector;
	bool bReturnBounceConsumed = false;
	bool bReturnBounceEligibleFlight = false;

	TMap<TObjectKey<AActor>, float> LastCombatHitTimes;
};
