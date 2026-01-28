// PlayerDamageCategory.h
// Damage category system for UI damage numbers

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PlayerDamageCategory.generated.h"

/**
 * Damage categories for player-dealt damage display
 * Used to color-code floating damage numbers
 */
UENUM(BlueprintType)
enum class EPlayerDamageCategory : uint8
{
	/** Base damage - standard melee and ranged attacks */
	Base		UMETA(DisplayName = "Base"),

	/** Kinetic damage - wallslam, momentum bonus, dropkick */
	Kinetic		UMETA(DisplayName = "Kinetic"),

	/** EMF damage - EMF proximity, EMF weapon */
	EMF			UMETA(DisplayName = "EMF")
};

/**
 * Helper class for damage category operations
 */
UCLASS()
class POLARITY_API UDamageCategoryHelper : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Convert a damage type class to its corresponding damage category
	 * @param DamageTypeClass The damage type class to categorize
	 * @return The damage category for UI display
	 */
	UFUNCTION(BlueprintPure, Category = "Damage|Category",
		meta = (DisplayName = "Get Category From Damage Type"))
	static EPlayerDamageCategory GetCategoryFromDamageType(TSubclassOf<UDamageType> DamageTypeClass);

	/**
	 * Get the display name for a damage category
	 * @param Category The damage category
	 * @return Human-readable name
	 */
	UFUNCTION(BlueprintPure, Category = "Damage|Category",
		meta = (DisplayName = "Get Category Display Name"))
	static FText GetCategoryDisplayName(EPlayerDamageCategory Category);
};
