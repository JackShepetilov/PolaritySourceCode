// PlayerSuppressionComponent.h
// What rounds passing close do to a player [author, 2026-10-11].
//
// The soldier the run starts with is afraid under fire, and that is what turns a fight into a
// firefight: the enemies that shoot to pin (two aim, the rest keep heads down) now have something to
// say. As the soldier grows the effect is to shrink (Resistance), letting him do wilder things under
// fire.
//
// What it does, and what it deliberately does not (the author's choice, the mild set):
//   does      the edges of the picture close in; the sight comes up slower; the sight sways
//   does not  touch walking, sprint or the slide. The slide is how the player gets INTO cover.
//   does not  widen the spread or move a round off a true aim. That is what Battlefield 3 did and
//             what its players hated; Battlefield 4 and Squad went to sway and tunnel vision.
//
// A round that is stopped by cover before it reaches the player does not count, so behind cover the
// level falls away by itself.
//
// Where it lives: the level is kept on the machine of the player it belongs to, which is where the
// sight and the picture are. The server works out the near miss (it is the one that fires the
// enemies' rounds), keeps its own copy for the record of the fight, and tells the owner.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PlayerSuppressionComponent.generated.h"

class APostProcessVolume;

UCLASS(ClassGroup = (Polarity), meta = (BlueprintSpawnableComponent))
class POLARITY_API UPlayerSuppressionComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	UPlayerSuppressionComponent();

	// ==================== Tuning ====================

	/** A round passing closer than this counts (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Suppression", meta = (ClampMin = "0.0", Units = "cm"))
	float NearMissRadius = 200.0f;

	/** What one round brushing past adds, of a full 1. Half of it for a round at the edge of the radius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Suppression", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LevelPerNearMiss = 0.22f;

	/** How long after the last near miss the level holds before it starts to fall (s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Suppression", meta = (ClampMin = "0.0", Units = "s"))
	float DecayDelay = 0.8f;

	/** How long a full level takes to fall to nothing once it has started (s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Suppression", meta = (ClampMin = "0.05", Units = "s"))
	float DecaySeconds = 2.0f;

	/** 0 takes everything, 1 takes nothing. What growing into a war dog raises. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Suppression", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Resistance = 0.0f;

	/** The speed the sight comes up at, at a full level (1 = unchanged). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Suppression|Effects", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float ADSSpeedAtFull = 0.45f;

	/** How far the sight wanders at a full level, while aiming (degrees, each way). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Suppression|Effects", meta = (ClampMin = "0.0", Units = "deg"))
	float SwayDegreesAtFull = 0.7f;

	/** How fast the sight wanders (cycles per second). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Suppression|Effects", meta = (ClampMin = "0.05"))
	float SwayFrequency = 0.55f;

	/** Engine vignette intensity at a full level. The engine's own default is 0.4. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Suppression|Effects", meta = (ClampMin = "0.0"))
	float VignetteAtFull = 1.4f;

	// ==================== API ====================

	/** A hostile round left Start along Direction. Every player it passes close to, with nothing in
	 *  between, is suppressed by it. Authority only; free to call for every enemy shot. */
	static void NotifyHostileShot(const AActor* Shooter, const FVector& Start, const FVector& Direction, float MaxRange = 15000.0f);

	/** Add to the level (before Resistance). Authority or the owning machine. */
	UFUNCTION(BlueprintCallable, Category = "Suppression")
	void AddSuppression(float Amount);

	/** 0 calm, 1 pinned. */
	UFUNCTION(BlueprintPure, Category = "Suppression")
	float GetLevel() const { return Level; }

	/** What the speed of raising the sight is multiplied by right now. */
	float GetADSSpeedMultiplier() const;

	/** What is added to the view while aiming: the wandering sight. ADSAlpha 0 gives nothing. */
	FRotator GetSwayRotation(float ADSAlpha) const;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:

	UFUNCTION(Client, Unreliable)
	void Client_AddSuppression(float Amount);

	void ApplyLocally(float Amount);
	void UpdateVignette();
	bool IsOwnerLocal() const;

	float Level = 0.0f;
	float LastNearMissTime = -1000.0f;

	/** The picture's edges. Unbound and spawned on the owner's machine only: a blendable on the
	 *  character's own camera is invisible most of the time (Docs/Gotchas/Camera_FirstPerson.md). */
	UPROPERTY(Transient)
	TObjectPtr<APostProcessVolume> VignetteVolume;
};
