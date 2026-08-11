// SportsBall.h
// Physics ball for arena sport objectives. Reacts only to unarmed melee hits.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Variant_Shooter/MeleeAttackComponent.h"
#include "SportsBall.generated.h"

class ASportsBall;
class AShooterCharacter;
class UPhysicalMaterial;
class UPrimitiveComponent;
class UStaticMeshComponent;
class UApexMovementComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_SixParams(
	FOnSportsBallMeleeHit,
	ASportsBall*, Ball,
	AActor*, Attacker,
	EMeleeAttackType, AttackType,
	bool, bAppliedKick,
	FVector, HitLocation,
	FVector, AppliedImpulse);

UCLASS(Blueprintable)
class POLARITY_API ASportsBall : public AActor
{
	GENERATED_BODY()

public:
	ASportsBall();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> BallMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Physics", meta = (ClampMin = "10.0", Units = "cm"))
	float BallDiameter = 70.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Physics", meta = (ClampMin = "0.1", Units = "kg"))
	float BallMassKg = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Physics", meta = (ClampMin = "0.0"))
	float LinearDamping = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Physics", meta = (ClampMin = "0.0"))
	float AngularDamping = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Physics")
	bool bUseCCD = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Physics")
	TObjectPtr<UPhysicalMaterial> BallPhysicalMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Kick", meta = (ClampMin = "0.0", Units = "cm/s"))
	float KickVelocityChange = 1400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Kick", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float KickUpwardBias = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Kick", meta = (ClampMin = "0.0"))
	float PlayerVelocityToKickScale = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Kick", meta = (ClampMin = "0.0", Units = "cm/s"))
	float MaxPlayerVelocityBonus = 700.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Kick", meta = (ClampMin = "0.0"))
	float KickSpinVelocityChange = 900.0f;

	/** Health restored to the player for each successful kick that moves the ball. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Healing", meta = (ClampMin = "0.0"))
	float KickHealAmount = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Kick|Slide", meta = (ClampMin = "0.0", Units = "cm/s"))
	float SlidingKickUpVelocityChange = 650.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Kick|Slide", meta = (ClampMin = "0.0", Units = "cm/s"))
	float SlidingKickForwardVelocityChange = 450.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Contact Push", meta = (ClampMin = "0.0", Units = "cm/s"))
	float SlidingBodyPushMinSpeed = 150.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Contact Push", meta = (ClampMin = "0.0"))
	float SlidingBodyPushVelocityScale = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Contact Push", meta = (ClampMin = "0.0", Units = "cm/s"))
	float SlidingBodyPushMaxVelocityChange = 450.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Contact Push", meta = (ClampMin = "0.0", Units = "cm/s"))
	float SlidingBodyPushMaxBallSpeed = 1800.0f;

	/** Health restored while the player moves the ball through ordinary body collision. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Healing", meta = (ClampMin = "0.0"))
	float ContactPushHealAmount = 2.0f;

	/** Minimum player speed toward the ball required for contact-push healing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Healing", meta = (ClampMin = "0.0", Units = "cm/s"))
	float ContactPushHealMinSpeed = 40.0f;

	/** Prevents persistent physics contact from healing every frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Healing", meta = (ClampMin = "0.0", Units = "s"))
	float ContactPushHealCooldown = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Debug")
	bool bLogMeleeHits = false;

	UPROPERTY(BlueprintReadOnly, Category = "Ball|State")
	int32 PunchHitCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Ball|State")
	int32 KickHitCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Ball|State")
	EMeleeAttackType LastMeleeAttackType = EMeleeAttackType::Ground;

	UPROPERTY(BlueprintReadOnly, Category = "Ball|State")
	bool bLastHitAppliedKick = false;

	UPROPERTY(BlueprintReadOnly, Category = "Ball|State")
	FVector LastAppliedImpulse = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Ball|State")
	FVector LastHitLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Ball|State")
	TObjectPtr<AActor> LastAttacker;

	UPROPERTY(BlueprintAssignable, Category = "Ball|Events")
	FOnSportsBallMeleeHit OnSportsBallMeleeHit;

	UFUNCTION(BlueprintCallable, Category = "Ball|Melee")
	bool HandleMeleeAttackHit(AActor* Attacker, const FHitResult& HitResult, EMeleeAttackType AttackType, const FVector& AttackDirection, const FVector& AttackerVelocity);

	UFUNCTION(BlueprintPure, Category = "Ball|Melee")
	static bool IsKickAttackType(EMeleeAttackType AttackType);

protected:
	void ApplyBallSettings();
	FVector BuildKickDirection(AActor* Attacker, const FVector& AttackDirection) const;
	bool IsSlidingCharacter(AActor* Actor, FVector& OutVelocity) const;
	void HealPlayerFromInteraction(AActor* PlayerActor, float Amount, const TCHAR* Source);

	float LastContactPushHealTime = -1000.0f;

	UFUNCTION()
	void OnBallHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		FVector NormalImpulse, const FHitResult& Hit);
};
