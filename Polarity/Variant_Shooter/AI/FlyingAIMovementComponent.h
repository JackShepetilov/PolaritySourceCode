// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FlyingAIMovementComponent.generated.h"

class UCharacterMovementComponent;
class ACharacter;

/** Delegate called when movement to target is completed */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnFlyingMovementCompleted, bool, bSuccess);

/** Delegate called when dash is completed */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnDashCompleted);

/**
 * Movement component for flying AI that works with CharacterMovementComponent in Flying mode.
 * Provides 3D navigation without requiring NavMesh for pathfinding.
 * Handles hover behavior, dash maneuvers, and patrol point generation.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class POLARITY_API UFlyingAIMovementComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	UFlyingAIMovementComponent();

protected:

	virtual void BeginPlay() override;

public:

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ==================== Height Settings ====================

	/** Minimum hover height above ground */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flying|Height")
	float MinHoverHeight = 200.0f;

	/** Maximum hover height above ground */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flying|Height")
	float MaxHoverHeight = 450.0f;

	/** Default hover height (used for patrol) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flying|Height")
	float DefaultHoverHeight = 300.0f;

	// ==================== Oscillation Settings ====================

	/** Enable sinusoidal vertical oscillation while hovering */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flying|Oscillation")
	bool bEnableHoverOscillation = false;

	/** Amplitude of hover oscillation (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flying|Oscillation", meta = (EditCondition = "bEnableHoverOscillation"))
	float HoverOscillationAmplitude = 30.0f;

	/** Frequency of hover oscillation (Hz) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flying|Oscillation", meta = (EditCondition = "bEnableHoverOscillation"))
	float HoverOscillationFrequency = 0.5f;

	// ==================== Movement Settings ====================

	/** Speed when flying normally */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flying|Movement")
	float FlySpeed = 600.0f;

	/** Speed when moving vertically */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flying|Movement")
	float VerticalSpeed = 400.0f;

	/** Acceleration for flying movement */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flying|Movement")
	float FlyAcceleration = 1000.0f;

	/** Deceleration when stopping */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flying|Movement")
	float FlyDeceleration = 500.0f;

	/** Distance threshold to consider target reached */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flying|Movement")
	float AcceptanceRadius = 100.0f;

	// ==================== Dash Settings ====================

	/** Speed during dash maneuver */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flying|Dash")
	float DashSpeed = 1500.0f;

	/** Duration of dash in seconds */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flying|Dash")
	float DashDuration = 0.3f;

	/** Cooldown between dashes */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flying|Dash")
	float DashCooldown = 2.0f;

	/** Minimum dash distance */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flying|Dash")
	float MinDashDistance = 200.0f;

	/** Maximum dash distance */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flying|Dash")
	float MaxDashDistance = 500.0f;

	// ==================== Flying Mode Control ====================

	/** If true, component will enforce MOVE_Flying every tick. Set to false when landing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flying|Movement")
	bool bEnforceFlyingMode = true;

	// ==================== Patrol Settings ====================

	/** Radius for random patrol point generation (horizontal) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flying|Patrol")
	float PatrolRadius = 1000.0f;

	/** If true, patrol points are relative to spawn location. If false, relative to current location */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flying|Patrol")
	bool bPatrolAroundSpawn = true;

	// ==================== Obstacle Avoidance ====================

	/** Enable simple obstacle avoidance via raycasts */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flying|Avoidance")
	bool bEnableObstacleAvoidance = true;

	/** Distance to check for obstacles ahead */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flying|Avoidance", meta = (EditCondition = "bEnableObstacleAvoidance"))
	float ObstacleCheckDistance = 300.0f;

	/** Collision channel for obstacle checks */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flying|Avoidance", meta = (EditCondition = "bEnableObstacleAvoidance"))
	TEnumAsByte<ECollisionChannel> ObstacleChannel = ECC_Visibility;

	// ==================== Ceiling Detection ====================

	/** Minimum clearance from ceiling (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flying|Height")
	float CeilingClearance = 100.0f;

	// ==================== NavMesh Projection ====================

	/** If true, validate that movement targets project onto NavMesh */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flying|NavMesh")
	bool bRequireNavMeshProjection = true;

	/** Maximum distance to search for NavMesh projection (horizontal) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flying|NavMesh", meta = (EditCondition = "bRequireNavMeshProjection"))
	float NavMeshProjectionRadius = 500.0f;

public:

	// ==================== Delegates ====================

	/** Called when FlyToLocation completes */
	UPROPERTY(BlueprintAssignable, Category = "Flying|Events")
	FOnFlyingMovementCompleted OnMovementCompleted;

	/** Called when dash completes */
	UPROPERTY(BlueprintAssignable, Category = "Flying|Events")
	FOnDashCompleted OnDashCompleted;

	// ==================== Movement Commands ====================

	/**
	 * Start flying to the specified location
	 * @param TargetLocation - World location to fly to
	 * @param CustomAcceptanceRadius - Override acceptance radius (-1 to use default)
	 */
	UFUNCTION(BlueprintCallable, Category = "Flying|Movement")
	void FlyToLocation(const FVector& TargetLocation, float CustomAcceptanceRadius = -1.0f);

	/**
	 * Start flying to the specified actor
	 * @param TargetActor - Actor to fly towards
	 * @param CustomAcceptanceRadius - Override acceptance radius (-1 to use default)
	 */
	UFUNCTION(BlueprintCallable, Category = "Flying|Movement")
	void FlyToActor(AActor* TargetActor, float CustomAcceptanceRadius = -1.0f);

	/** Stop current movement */
	UFUNCTION(BlueprintCallable, Category = "Flying|Movement")
	void StopMovement();

	/**
	 * Perform a dash maneuver in 3D space
	 * @param Direction - World space direction to dash (will be normalized)
	 * @return True if dash started successfully
	 */
	UFUNCTION(BlueprintCallable, Category = "Flying|Dash")
	bool StartDash(const FVector& Direction);

	/**
	 * Perform an evasive dash away from the threat
	 * @param ThreatLocation - Location to evade from
	 * @return True if dash started successfully
	 */
	UFUNCTION(BlueprintCallable, Category = "Flying|Dash")
	bool StartEvasiveDash(const FVector& ThreatLocation);

	// ==================== Patrol & Point Generation ====================

	/**
	 * Generate a random 3D point within the patrol volume
	 * @param OutPoint - The generated point
	 * @return True if a valid point was found
	 */
	UFUNCTION(BlueprintCallable, Category = "Flying|Patrol")
	bool GetRandomPatrolPoint(FVector& OutPoint);

	/**
	 * Generate a random point in 3D space around a center
	 * @param Center - Center point
	 * @param HorizontalRadius - Radius on XY plane
	 * @param MinHeight - Minimum height offset
	 * @param MaxHeight - Maximum height offset
	 * @param OutPoint - The generated point
	 * @return True if valid point was found
	 */
	UFUNCTION(BlueprintCallable, Category = "Flying|Patrol")
	bool GetRandomPointInVolume(const FVector& Center, float HorizontalRadius, float MinHeight, float MaxHeight, FVector& OutPoint);

	// ==================== State Queries ====================

	/** Returns true if currently flying to a target */
	UFUNCTION(BlueprintPure, Category = "Flying|State")
	bool IsMoving() const { return bIsMovingToTarget; }

	/** Returns true if currently dashing */
	UFUNCTION(BlueprintPure, Category = "Flying|State")
	bool IsDashing() const { return bIsDashing; }

	/** Returns true if dash is on cooldown */
	UFUNCTION(BlueprintPure, Category = "Flying|State")
	bool IsDashOnCooldown() const;

	/** Returns current target location */
	UFUNCTION(BlueprintPure, Category = "Flying|State")
	FVector GetTargetLocation() const { return CurrentTargetLocation; }

	/** Returns spawn location (for patrol calculations) */
	UFUNCTION(BlueprintPure, Category = "Flying|State")
	FVector GetSpawnLocation() const { return SpawnLocation; }

	/** Set a new home/spawn location for patrol */
	UFUNCTION(BlueprintCallable, Category = "Flying|Patrol")
	void SetHomeLocation(const FVector& NewHome) { SpawnLocation = NewHome; }

	/** Check if XY position projects onto NavMesh, returns projected point */
	UFUNCTION(BlueprintCallable, Category = "Flying|NavMesh")
	bool ProjectToNavMesh(const FVector& Location, FVector& OutProjectedLocation) const;

protected:

	// ==================== Internal State ====================

	/** Cached character owner */
	UPROPERTY()
	TObjectPtr<ACharacter> CharacterOwner;

	/** Cached movement component */
	UPROPERTY()
	TObjectPtr<UCharacterMovementComponent> MovementComponent;

	/** Location where this AI spawned */
	FVector SpawnLocation;

	/** Current target location for movement */
	FVector CurrentTargetLocation;

	/** Actor being followed (if any) */
	TWeakObjectPtr<AActor> TargetActor;

	/** Current acceptance radius for movement */
	float CurrentAcceptanceRadius;

	/** Is currently moving to a target */
	bool bIsMovingToTarget = false;

	/** Is currently performing a dash */
	bool bIsDashing = false;

	/** Direction of current dash */
	FVector DashDirection;

	/** Start position of dash (for interpolation) */
	FVector DashStartPosition;

	/** Target position of dash (for interpolation) */
	FVector DashTargetPosition;

	/** Elapsed time during dash */
	float DashElapsedTime = 0.0f;

	/** Time when last dash ended (for cooldown) */
	float LastDashEndTime = 0.0f;

	/** Time accumulator for oscillation */
	float OscillationTime = 0.0f;

	// ==================== Internal Methods ====================

	/** Update movement towards target */
	void UpdateMovement(float DeltaTime);

	/** Update dash movement */
	void UpdateDash(float DeltaTime);

	/** Apply hover oscillation */
	void ApplyHoverOscillation(float DeltaTime);

	/** Check for obstacles and adjust direction if needed */
	FVector GetAvoidanceAdjustedDirection(const FVector& DesiredDirection);

	/** Get height above ground at given location */
	float GetHeightAboveGround(const FVector& Location) const;

	/** Validate and adjust target location to be within height bounds (floor and ceiling) */
	FVector ValidateTargetHeight(const FVector& TargetLocation) const;

	/** Get height to ceiling at given location (returns MAX_FLT if no ceiling) */
	float GetHeightToCeiling(const FVector& Location) const;

	/** Apply movement input to character with collision checking */
	void ApplyMovementInput(const FVector& Direction, float Speed);

	/** Check if movement in direction would cause collision, returns safe direction */
	FVector GetCollisionSafeDirection(const FVector& DesiredDirection, float Speed, float DeltaTime) const;

	/** Complete current movement */
	void CompleteMovement(bool bSuccess);

	/** Complete current dash */
	void CompleteDash();
};