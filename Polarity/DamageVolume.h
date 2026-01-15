// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/BoxComponent.h"
#include "DamageVolume.generated.h"

/**
 * Volume that deals damage to overlapping actors over time
 * Compatible with ShooterCharacter damage system
 */
UCLASS()
class POLARITY_API ADamageVolume : public AActor
{
	GENERATED_BODY()

public:
	ADamageVolume();

protected:
	virtual void BeginPlay() override;

	/** Box collision for overlap detection */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> DamageBox;

	/** Damage dealt per tick */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage", meta = (ClampMin = "0.0"))
	float DamagePerTick = 10.0f;

	/** Time between damage ticks in seconds */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage", meta = (ClampMin = "0.01"))
	float DamageInterval = 0.5f;

	/** Damage type class to apply */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage")
	TSubclassOf<UDamageType> DamageTypeClass;

	/** If true, damage is enabled */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage")
	bool bDamageEnabled = true;

private:
	/** Actors currently inside the volume */
	UPROPERTY()
	TArray<TWeakObjectPtr<AActor>> OverlappingActors;

	/** Timer handle for damage ticks */
	FTimerHandle DamageTimerHandle;

	UFUNCTION()
	void OnBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	/** Called on timer to deal damage to all overlapping actors */
	void DealDamage();

public:
	/** Enable or disable damage */
	UFUNCTION(BlueprintCallable, Category = "Damage")
	void SetDamageEnabled(bool bEnabled);
};
