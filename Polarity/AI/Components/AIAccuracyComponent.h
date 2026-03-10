// AIAccuracyComponent.h
// NPC accuracy system based on target speed

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AIAccuracyComponent.generated.h"

class UCurveFloat;
class ACharacter;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSuppressionStart, float, Duration);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSuppressionEnd);

/**
 * Component that calculates NPC aim accuracy based on target's movement speed.
 * Faster targets are harder to hit. Wall running provides additional spread bonus.
 * Supports suppression state: forced donut-pattern misses around the target.
 */
UCLASS(ClassGroup = (AI), meta = (BlueprintSpawnableComponent))
class POLARITY_API UAIAccuracyComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAIAccuracyComponent();

	// ==================== Settings ====================

	/** Base spread when target is stationary (degrees) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float BaseSpread = 2.0f;

	/** Maximum spread at max target speed (degrees) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy", meta = (ClampMin = "0.0", ClampMax = "45.0"))
	float MaxSpread = 20.0f;

	/**
	 * Curve mapping normalized speed (0-1) to spread multiplier (0-1).
	 * X = target speed / MaxTargetSpeed
	 * Y = spread interpolation factor
	 * If null, linear interpolation is used.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy")
	TObjectPtr<UCurveFloat> SpeedToSpreadCurve;

	/** Target speed considered maximum for normalization (cm/s) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy", meta = (ClampMin = "100.0"))
	float MaxTargetSpeed = 1200.0f;

	/**
	 * Distribution curve for shot placement within the spread cone.
	 * X = random value (0-1)
	 * Y = distance from center (0 = center, 1 = edge of cone)
	 * Use to bias shots toward center or edges.
	 * If null, uniform distribution is used.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy")
	TObjectPtr<UCurveFloat> SpreadDistributionCurve;

	/** Additional spread multiplier when target is wall running */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy|Modifiers", meta = (ClampMin = "1.0", ClampMax = "3.0"))
	float WallRunSpreadMultiplier = 1.3f;

	/** Additional spread multiplier when target is in the air (jumping/falling) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy|Modifiers", meta = (ClampMin = "1.0", ClampMax = "3.0"))
	float InAirSpreadMultiplier = 1.2f;

	// ==================== Suppression Settings ====================

	/** Minimum deviation angle during suppression — guarantees a miss (degrees) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy|Suppression", meta = (ClampMin = "1.0", ClampMax = "30.0"))
	float MinSuppressionSpread = 8.0f;

	/** Maximum deviation angle during suppression — keeps shots visually near the target (degrees) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy|Suppression", meta = (ClampMin = "5.0", ClampMax = "45.0"))
	float MaxSuppressionSpread = 20.0f;

	// ==================== Runtime State ====================

	/** Last calculated spread value (for debugging) */
	UPROPERTY(BlueprintReadOnly, Category = "Accuracy|Debug")
	float LastCalculatedSpread = 0.0f;

	/** Last target speed ratio (for debugging) */
	UPROPERTY(BlueprintReadOnly, Category = "Accuracy|Debug")
	float LastSpeedRatio = 0.0f;

	// ==================== Suppression Delegates ====================

	/** Broadcast when suppression begins. Duration = total suppression time. */
	UPROPERTY(BlueprintAssignable, Category = "Accuracy|Suppression")
	FOnSuppressionStart OnSuppressionStart;

	/** Broadcast when suppression ends (timer expired or manually cleared). */
	UPROPERTY(BlueprintAssignable, Category = "Accuracy|Suppression")
	FOnSuppressionEnd OnSuppressionEnd;

	// ==================== API ====================

	/**
	 * Calculate aim direction with accuracy spread applied.
	 * During suppression, uses donut pattern instead of normal spread.
	 * @param TargetLocation - World location to aim at
	 * @param Target - Target actor (used to get velocity and movement state)
	 * @return Direction vector with spread applied
	 */
	UFUNCTION(BlueprintCallable, Category = "Accuracy")
	FVector CalculateAimDirection(const FVector& TargetLocation, AActor* Target);

	/**
	 * Get current effective spread for a target (degrees).
	 * Does not apply randomization, just returns the spread angle.
	 */
	UFUNCTION(BlueprintPure, Category = "Accuracy")
	float GetCurrentSpread(AActor* Target) const;

	/**
	 * Get target's normalized speed (0-1 range).
	 */
	UFUNCTION(BlueprintPure, Category = "Accuracy")
	float GetTargetSpeedRatio(AActor* Target) const;

	/**
	 * Check if target is currently wall running.
	 */
	UFUNCTION(BlueprintPure, Category = "Accuracy")
	bool IsTargetWallRunning(AActor* Target) const;

	/**
	 * Check if target is currently in the air (jumping/falling).
	 */
	UFUNCTION(BlueprintPure, Category = "Accuracy")
	bool IsTargetInAir(AActor* Target) const;

	/**
	 * Apply spread to a direction vector.
	 * @param BaseDirection - Original aim direction (normalized)
	 * @param SpreadDegrees - Spread cone half-angle in degrees
	 * @return New direction with random offset within cone
	 */
	UFUNCTION(BlueprintCallable, Category = "Accuracy")
	FVector ApplySpreadToDirection(const FVector& BaseDirection, float SpreadDegrees) const;

	// ==================== Suppression API ====================

	/**
	 * Apply suppression: forces donut-pattern misses for the given duration.
	 * Stacks with diminishing returns if already suppressed.
	 * @param Duration - How long the suppression lasts (seconds)
	 * @param DiminishingFactor - Stacking falloff rate. Each stack adds Duration / (1 + StackCount * Factor)
	 */
	UFUNCTION(BlueprintCallable, Category = "Accuracy|Suppression")
	void ApplySuppression(float Duration, float DiminishingFactor = 0.5f);

	/** Immediately clear suppression state */
	UFUNCTION(BlueprintCallable, Category = "Accuracy|Suppression")
	void ClearSuppression();

	/** Returns true if currently suppressed */
	UFUNCTION(BlueprintPure, Category = "Accuracy|Suppression")
	bool IsSuppressed() const { return bIsSuppressed; }

	/** Returns remaining suppression time (0 if not suppressed) */
	UFUNCTION(BlueprintPure, Category = "Accuracy|Suppression")
	float GetSuppressionTimeRemaining() const { return SuppressionTimeRemaining; }

protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Get aim origin (owner's location, typically eye height) */
	FVector GetAimOrigin() const;

	/** Sample the spread distribution curve */
	float SampleSpreadDistribution() const;

	/**
	 * Apply donut-shaped spread: shots land between MinSpread and MaxSpread degrees from center.
	 * Guarantees a miss (minimum deviation) while keeping shots visually close to the target.
	 */
	FVector ApplyDonutSpread(const FVector& BaseDirection, float InnerSpread, float OuterSpread) const;

private:
	/** True when NPC is in suppression state (forced misses) */
	bool bIsSuppressed = false;

	/** Seconds remaining in current suppression */
	float SuppressionTimeRemaining = 0.0f;

	/** Number of suppression applications during current suppression (for diminishing returns) */
	int32 SuppressionStackCount = 0;
};