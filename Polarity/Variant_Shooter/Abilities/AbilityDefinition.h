// AbilityDefinition.h
// Abstract base for ability definitions. Holds only fields that ALL abilities share.
// Per-archetype data (burst-iterating, channeled, instant-effect, etc.) lives in subclasses
// like UAbilityDefinition_Burst.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "AbilityDefinition.generated.h"

class UAnimMontage;
class UTexture2D;
class UAbilityHandler;

/**
 * How the activation button is interpreted.
 *  Tap   — activate on press, ignore release.
 *  Hold  — activate on press, release ends activation (e.g. shield, channeled beam).
 */
UENUM(BlueprintType)
enum class EAbilityActivationMode : uint8
{
	Tap,
	Hold
};

/**
 * Universal per-level stats. Every ability has a cooldown and a charge gate, so these live
 * on the base. Archetype-specific stats (per-shot cost, projectile count, channel duration)
 * live in subclass-defined level structs.
 */
USTRUCT(BlueprintType)
struct FAbilityCommonStats
{
	GENERATED_BODY()

	/** Common cooldown after the ability completes successfully (seconds). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Common", meta = (ClampMin = "0.0", ClampMax = "60.0", Units = "s"))
	float Cooldown = 5.0f;

	/** Minimum |charge| in player's EMF pool required to start activation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Common", meta = (ClampMin = "0.0"))
	float MinimumChargeToActivate = 3.0f;
};

/**
 * Abstract base. DataAsset author cannot create one of these directly — they create a subclass
 * matching their ability archetype (e.g. UAbilityDefinition_Burst).
 */
UCLASS(BlueprintType, Abstract)
class POLARITY_API UAbilityDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:

	// ==================== Identity ====================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity")
	FGameplayTag AbilityTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity", meta = (MultiLine = true))
	FText Description;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity")
	TObjectPtr<UTexture2D> Icon;

	// ==================== Runtime ====================

	/** Handler class instantiated by UAbilityComponent. Subclasses can narrow this via
	 *  meta = (AllowedClasses="...") in their own UPROPERTY override if needed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Runtime")
	TSubclassOf<UAbilityHandler> HandlerClass;

	// ==================== Activation ====================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Activation")
	EAbilityActivationMode ActivationMode = EAbilityActivationMode::Tap;

	// ==================== Animation pipeline (shared) ====================
	// Most abilities have an intro and outro animation on the FirstPersonMesh's left arm.
	// Archetype-specific intermediate animations (loop-shot, channel-tick) live in subclasses.

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	TObjectPtr<UAnimMontage> CastStartMontage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	TObjectPtr<UAnimMontage> CastFinishMontage;

	// ==================== Audio (shared) ====================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	TObjectPtr<USoundBase> CastStartSound;

	// ==================== Level API (overridden by subclasses) ====================

	/** Number of authored levels. Subclass returns its Levels.Num(). Default 1 for abilities
	 *  that don't differentiate by level. */
	UFUNCTION(BlueprintPure, Category = "Levels")
	virtual int32 GetMaxLevel() const { return 1; }

	/** Universal stats at given (1-based) level. Default returns defaulted struct.
	 *  Subclass overrides to return values from its per-level array. */
	UFUNCTION(BlueprintPure, Category = "Levels")
	virtual FAbilityCommonStats GetCommonStatsAtLevel(int32 Level) const { return FAbilityCommonStats{}; }
};
