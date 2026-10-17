// FootstepNoiseComponent.h
// [author, 2026-10-14] A player who runs, slides or lands hard is heard by the soldiers around,
// through walls. Walking and crouching are quiet. What a soldier does with it: it turns to the
// sound and holds its gun there, so whoever comes in that way meets a gun already on him.
// @see AShooterAIController::ReportMovementNoise, UAIAccuracyComponent (Readiness)

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FootstepNoiseComponent.generated.h"

UCLASS(ClassGroup = (AI), meta = (BlueprintSpawnableComponent))
class POLARITY_API UFootstepNoiseComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	UFootstepNoiseComponent();

	/** On the ground at this speed or faster it is running (cm/s). Walking is 600. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise", meta = (ClampMin = "0.0", Units = "cm/s"))
	float RunSpeed = 700.0f;

	/** Heard this far while running (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise", meta = (ClampMin = "0.0", Units = "cm"))
	float RunRange = 1000.0f;

	/** Heard this far while walking, not crouched (cm). 0: walking is silent. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise", meta = (ClampMin = "0.0", Units = "cm"))
	float WalkRange = 0.0f;

	/** Heard this far while sliding (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise", meta = (ClampMin = "0.0", Units = "cm"))
	float SlideRange = 1400.0f;

	/** Heard this far while running along a wall (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise", meta = (ClampMin = "0.0", Units = "cm"))
	float WallRunRange = 1000.0f;

	/** Landing at this fall speed or faster is heard (cm/s)... */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise", meta = (ClampMin = "0.0", Units = "cm/s"))
	float HardLandingSpeed = 600.0f;

	/** ...this far (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise", meta = (ClampMin = "0.0", Units = "cm"))
	float LandingRange = 1200.0f;

	/** How often a running or sliding body is heard again (s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise", meta = (ClampMin = "0.05", Units = "s"))
	float Interval = 0.3f;

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:

	bool bWasFalling = false;
	float FallSpeed = 0.0f;
	float NextNoiseTime = 0.0f;
};
