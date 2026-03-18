// PolarityPathFollowingComponent.h
// Custom path following that handles NavLink traversal via character jumps

#pragma once

#include "CoreMinimal.h"
#include "Navigation/PathFollowingComponent.h"
#include "PolarityPathFollowingComponent.generated.h"

class ACharacter;

/**
 * Custom PathFollowingComponent that detects NavLink segments in the navigation path
 * and executes jumps (via LaunchCharacter) to traverse them.
 *
 * Works transparently with both:
 * - Automatic Navigation Link Generation (GeneratedNavLinksProxy, UE 5.4+)
 * - Manual NavLinkProxy actors (Smart Links)
 *
 * The StateTree/BehaviorTree MoveTo tasks work unchanged — the jump happens
 * at the path-following level when a NavLink segment is encountered.
 */
UCLASS()
class POLARITY_API UPolarityPathFollowingComponent : public UPathFollowingComponent
{
	GENERATED_BODY()

public:

	UPolarityPathFollowingComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	// ==================== Jump Parameters ====================

	/** Arc height parameter for jump trajectory (0.0 = flat, 0.5 = medium arc, 1.0 = high arc) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jump Navigation", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float JumpArcParam = 0.5f;

	/** Maximum time allowed for a single jump before force-completing (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jump Navigation", meta = (ClampMin = "1.0", ClampMax = "10.0"))
	float MaxJumpDuration = 3.0f;

	/** Minimum time after launch before checking for landing (prevents instant-land detection) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jump Navigation", meta = (ClampMin = "0.05", ClampMax = "0.5"))
	float LandingCheckDelay = 0.15f;

	/** Distance tolerance for considering jump destination reached (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jump Navigation", meta = (ClampMin = "10.0"))
	float JumpLandingTolerance = 150.0f;

	/** When false, NavLink jumps are blocked — NPC will repath without NavLinks.
	 *  Set to false during combat with LoS so ShooterNPC stays on the same level. */
	UPROPERTY(BlueprintReadWrite, Category = "Jump Navigation")
	bool bAllowNavLinkJumps = true;

	/** Draw debug visuals for jumps */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jump Navigation|Debug")
	bool bDebugJumps = false;

	// ==================== State Queries ====================

	/** Returns true if currently performing a navigation jump */
	UFUNCTION(BlueprintPure, Category = "Jump Navigation")
	bool IsPerformingJump() const { return bIsPerformingJump; }

	/** Cancel current jump (e.g. on death or knockback) */
	void CancelJump();

protected:

	// ==================== PathFollowingComponent Overrides ====================

	/** Detect NavLink segments and initiate jumps */
	virtual void SetMoveSegment(int32 SegmentStartIndex) override;

	/** During jump: skip normal movement. Otherwise: default behavior */
	virtual void FollowPathSegment(float DeltaTime) override;

	/** During jump: monitor landing. Otherwise: default behavior */
	virtual void UpdatePathSegment() override;

	/** Reset jump state when path following ends (abort, completion, etc.) */
	virtual void OnPathFinished(const FPathFollowingResult& Result) override;

private:

	// ==================== Jump State ====================

	/** True while character is in the air performing a NavLink jump */
	bool bIsPerformingJump = false;

	/** Destination point of the current jump (NavLink end) */
	FVector JumpDestination = FVector::ZeroVector;

	/** Time elapsed since jump launch */
	float JumpTimeElapsed = 0.0f;

	// ==================== Internal ====================

	/** Launch the character along a parabolic arc towards EndPos */
	void ExecuteJump(const FVector& EndPos);

	/** Check if the character has landed on the ground */
	bool HasCharacterLanded() const;

	/** Restore CMC and collision after landing */
	void LandCharacter(ACharacter* Character);

	/** Finalize the jump and resume normal path following */
	void CompleteJump();
};
