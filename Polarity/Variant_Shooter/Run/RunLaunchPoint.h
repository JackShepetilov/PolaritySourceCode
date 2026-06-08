// RunLaunchPoint.h
// Editor-placed marker for the start-of-run sea toss.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RunLaunchPoint.generated.h"

class UArrowComponent;

/**
 * Editor-placed marker that tags a level as a "run map" and carries its per-map run data.
 * The GameMode finds it on BeginPlay; its presence = run map (vs. menu/hub). On the world-ready
 * event the run-start sequence fires with this actor's ArenaIndex / bLaunchFromSea. When
 * bLaunchFromSea is set (first map of a run), the player is teleported here and tossed along the
 * actor's forward direction at LaunchSpeed (pitch the actor up / toward the island to author the
 * arc; the cyan arrow shows the launch vector).
 */
UCLASS()
class POLARITY_API ARunLaunchPoint : public AActor
{
	GENERATED_BODY()

public:
	ARunLaunchPoint();

	/** Which arena this map represents; passed to RunSubsystem::EnterArena on the world-ready event. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Run")
	int32 ArenaIndex = 0;

	/** First map of a run (player is tossed out of the sea). False on later maps = normal spawn, no toss. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Run")
	bool bLaunchFromSea = true;

	/** This map opens with a boss intro cutscene (camera cuts to the boss, blends back to the player,
	 *  then the player draws their weapon and the boss aggros). Drives the boss branch of
	 *  BP_OnRunStartReady. Typically paired with bLaunchFromSea = false. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Run")
	bool bBossIntro = false;

	/** Launch speed (uu/s) applied along the actor's forward direction (used when bLaunchFromSea). */
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
