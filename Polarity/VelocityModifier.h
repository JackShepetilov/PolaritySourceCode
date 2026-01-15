// VelocityModifier.h
// Интерфейс для интеграции EMF плагина с системой движения

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "VelocityModifier.generated.h"

UINTERFACE(MinimalAPI, Blueprintable)
class UVelocityModifier : public UInterface
{
	GENERATED_BODY()
};

class POLARITY_API IVelocityModifier
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, Category = "EMF")
	bool ModifyVelocity(float DeltaTime, const FVector& CurrentVelocity, FVector& OutVelocityDelta);
	virtual bool ModifyVelocity_Implementation(float DeltaTime, const FVector& CurrentVelocity, FVector& OutVelocityDelta) { return false; }

	UFUNCTION(BlueprintNativeEvent, Category = "EMF")
	float GetAccelerationMultiplier();
	virtual float GetAccelerationMultiplier_Implementation() { return 1.0f; }

	UFUNCTION(BlueprintNativeEvent, Category = "EMF")
	FVector GetExternalForce();
	virtual FVector GetExternalForce_Implementation() { return FVector::ZeroVector; }
};
