// RunLaunchPoint.h
// Editor-placed marker for the start-of-run sea toss.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RunLaunchPoint.generated.h"

class UArrowComponent;

/**
 * Editor-placed marker for the opening sea-toss. The player is teleported to this actor and
 * launched along its forward direction at LaunchSpeed. Place it above the water and pitch the
 * whole actor up / toward the island to author the arc (the cyan arrow shows the launch vector).
 */
UCLASS()
class POLARITY_API ARunLaunchPoint : public AActor
{
	GENERATED_BODY()

public:
	ARunLaunchPoint();

	/** Launch speed (uu/s) applied along the actor's forward direction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Launch")
	float LaunchSpeed = 3000.f;

	/** World-space launch velocity fed to LaunchCharacter: forward * LaunchSpeed. */
	UFUNCTION(BlueprintPure, Category = "Launch")
	FVector GetLaunchVelocity() const;

protected:
	UPROPERTY(VisibleAnywhere, Category = "Launch")
	TObjectPtr<USceneComponent> SceneRoot;

#if WITH_EDITORONLY_DATA
	/** Editor-only visualization of the launch direction. */
	UPROPERTY(VisibleAnywhere, Category = "Launch")
	TObjectPtr<UArrowComponent> DirectionArrow;
#endif
};
