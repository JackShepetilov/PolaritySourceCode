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

	/** Apex clearance (uu) the jump peaks ABOVE the higher endpoint. The arc is then
	 *  only as tall as needed — ~120-180 = a clean hop matching the obstacle, larger =
	 *  floatier. This drives the trajectory directly (see ExecuteJump); much more
	 *  predictable than an abstract 0..1 arc factor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jump Navigation", meta = (ClampMin = "30.0", ClampMax = "600.0"))
	float JumpApexClearance = 150.0f;

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

	/** While airborne on a navlink jump, ignore AI-initiated aborts so the ballistic arc completes.
	 *  The StateTree churning out of Pursue mid-flight (Move To finishing at its reach radius, a sibling
	 *  transition firing as the NPC rises, StopMovement) was stopping the climb ~0.2s in and dropping the
	 *  melee back down = the "stomp near the navlink". The jump self-terminates on landing or the
	 *  MaxJumpDuration timeout, so this cannot strand. */
	virtual void AbortMove(const UObject& Instigator, FPathFollowingResultFlags::Type AbortFlags, FAIRequestID RequestID = FAIRequestID::CurrentRequest, EPathFollowingVelocityMode VelocityMode = EPathFollowingVelocityMode::Reset) override;

	/** While airborne on a navlink jump, defer re-issued MoveTo requests (the Pursue Move To task
	 *  re-pathing to the moving player) rather than letting them supersede the jump via NewRequest —
	 *  that was the second abort route (flags=136) that killed the climb. Returns the in-flight move's
	 *  id so the caller keeps waiting on it; requests after landing go through normally. */
	virtual FAIRequestID RequestMove(const FAIMoveRequest& RequestData, FNavPathSharedPtr InPath) override;

	/** Drive the navlink jump from the component tick, INDEPENDENT of the path-following move. The base
	 *  only ticks FollowPathSegment/UpdatePathSegment while Status==Moving; the AI tearing the move down
	 *  mid-flight (via routes we can't all intercept) stopped that driving and dropped the NPC. Running
	 *  the arc here makes it land regardless of how/whether the move ends. Skips Super while airborne. */
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

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
