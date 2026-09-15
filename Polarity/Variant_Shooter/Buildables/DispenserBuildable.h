#pragma once

#include "CoreMinimal.h"
#include "BuildableActor.h"
#include "DispenserBuildable.generated.h"

class AShooterCharacter;

/** A data driven dispenser shell. Place a BP subclass and assign its mesh/definition. */
UCLASS(Blueprintable)
class POLARITY_API ADispenserBuildable : public ABuildableActor
{
	GENERATED_BODY()

public:
	ADispenserBuildable();

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Dispenser|Healing", meta = (ClampMin = "0.0"))
	float HealPerSecond = 12.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Dispenser|Healing", meta = (ClampMin = "0.0", Units = "cm"))
	float ServiceRadius = 350.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Dispenser|Ammo", meta = (ClampMin = "0.0"))
	float AmmoRoundsPerSecond = 2.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Dispenser|Ammo", meta = (ClampMin = "0"))
	int32 Fuel = 0;
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Dispenser|Ammo", meta = (ClampMin = "1"))
	int32 FuelPerSacrificedWeapon = 60;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dispenser")
	void AddFuel(int32 Amount);
	UFUNCTION(BlueprintPure, Category = "Dispenser")
	int32 GetFuel() const { return Fuel; }

protected:
	virtual void TickActive(float DeltaSeconds) override;

private:
	float AmmoAccumulator = 0.0f;
};
