// BenchWeaponRack.h
// One place of the armoury on the bench level: a gun lies here to be picked up, and when it has been
// taken another one of the same class appears [author, 2026-10-11].
//
// Hand-placed, one actor per gun, the class set in the Details panel. The gun on it is an ordinary
// drop (ADroppedRangedWeapon::SpawnFor), so picking it up is the game's own pick-up. Server only.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BenchWeaponRack.generated.h"

class ADroppedRangedWeapon;
class AShooterWeapon;
class UTextRenderComponent;

UCLASS()
class POLARITY_API ABenchWeaponRack : public AActor
{
	GENERATED_BODY()

public:

	ABenchWeaponRack();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rack")
	TSubclassOf<AShooterWeapon> WeaponClass;

	/** Shown over the gun. Empty: the class name. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rack")
	FString Title;

	/** How often the rack looks whether its gun is still there (s). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rack", meta = (ClampMin = "0.2", Units = "s"))
	float RefillInterval = 1.5f;

	/** A gun further than this from the rack counts as taken (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rack", meta = (ClampMin = "0.0", Units = "cm"))
	float TakenDistance = 250.0f;

protected:

	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rack")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rack")
	TObjectPtr<UTextRenderComponent> Label;

private:

	void Refill();

	TWeakObjectPtr<ADroppedRangedWeapon> Drop;
	FTimerHandle RefillTimer;
};
