// ShooterDummyInterface.h
// Interface for dummy targets that grant stable (non-decaying) charge on melee hit

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "ShooterDummyInterface.generated.h"

UINTERFACE(MinimalAPI, Blueprintable)
class UShooterDummyTarget : public UInterface
{
	GENERATED_BODY()
};

/**
 * Interface for targets that grant stable charge on melee hits.
 * Implement this on training dummies and similar targets.
 */
class POLARITY_API IShooterDummyTarget
{
	GENERATED_BODY()

public:

	/** Returns true if this target grants stable (non-decaying) charge on melee hit */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Dummy")
	bool GrantsStableCharge() const;

	/** Returns the amount of stable charge to grant per melee hit */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Dummy")
	float GetStableChargeAmount() const;

	/** Returns the amount of stable charge to grant on kill (0 = no bonus) */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Dummy")
	float GetKillChargeBonus() const;

	/** Returns true if this target is dead */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Dummy")
	bool IsDummyDead() const;
};
