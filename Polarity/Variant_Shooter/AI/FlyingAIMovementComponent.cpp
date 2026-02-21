// Copyright Epic Games, Inc. All Rights Reserved.

#include "FlyingAIMovementComponent.h"
#include "FlyingDrone.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "NavigationSystem.h"

UFlyingAIMovementComponent::UFlyingAIMovementComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UFlyingAIMovementComponent::BeginPlay()
{
	Super::BeginPlay();

	// Cache owner references
	CharacterOwner = Cast<ACharacter>(GetOwner());
	if (CharacterOwner)
	{
		MovementComponent = CharacterOwner->GetCharacterMovement();

		// Store spawn location
		SpawnLocation = CharacterOwner->GetActorLocation();

		// Configure CharacterMovementComponent for flying
		if (MovementComponent)
		{
			MovementComponent->SetMovementMode(MOVE_Flying);
			MovementComponent->GravityScale = 0.0f;
			MovementComponent->MaxFlySpeed = FlySpeed;
			MovementComponent->BrakingDecelerationFlying = FlyDeceleration;
			MovementComponent->MaxAcceleration = FlyAcceleration;

			// Enable better collision detection for flying
			MovementComponent->bAlwaysCheckFloor = false;
			MovementComponent->bUseFlatBaseForFloorChecks = false;

			// Reduce max step height to prevent climbing through geometry
			MovementComponent->MaxStepHeight = 0.0f;

			// Enable sub-stepping for better collision detection at high speeds
			MovementComponent->MaxSimulationIterations = 4;
			MovementComponent->MaxSimulationTimeStep = 0.025f;
		}
	}

	// Randomize oscillation start phase
	OscillationTime = FMath::RandRange(0.0f, 2.0f * PI);
}

void UFlyingAIMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!CharacterOwner || !MovementComponent)
	{
		return;
	}

	// Check if owner is in knockback - don't interfere with knockback physics
	if (AFlyingDrone* Drone = Cast<AFlyingDrone>(CharacterOwner))
	{
		if (Drone->IsInKnockback())
		{
			return; // Let knockback physics handle movement
		}
	}

	// Ensure we stay in flying mode (can be disabled for landing)
	if (bEnforceFlyingMode && MovementComponent->MovementMode != MOVE_Flying)
	{
		MovementComponent->SetMovementMode(MOVE_Flying);
	}

	// Update dash if active
	if (bIsDashing)
	{
		UpdateDash(DeltaTime);
		return; // Dash takes priority
	}

	// Update movement to target
	if (bIsMovingToTarget)
	{
		UpdateMovement(DeltaTime);
	}

	// Apply hover oscillation when idle or moving
	if (bEnableHoverOscillation && !bIsDashing)
	{
		ApplyHoverOscillation(DeltaTime);
	}
}

// ==================== Movement Commands ====================

void UFlyingAIMovementComponent::FlyToLocation(const FVector& TargetLocation, float CustomAcceptanceRadius)
{
	if (!CharacterOwner)
	{
		return;
	}

	// Clear actor target
	TargetActor.Reset();

	// Validate and set target
	CurrentTargetLocation = ValidateTargetHeight(TargetLocation);
	CurrentAcceptanceRadius = (CustomAcceptanceRadius > 0.0f) ? CustomAcceptanceRadius : AcceptanceRadius;
	bIsMovingToTarget = true;

	// Reset stuck detection for new movement
	StuckCheckTime = 0.0f;
}

void UFlyingAIMovementComponent::FlyToActor(AActor* InTargetActor, float CustomAcceptanceRadius)
{
	if (!CharacterOwner || !InTargetActor)
	{
		return;
	}

	TargetActor = InTargetActor;
	CurrentTargetLocation = ValidateTargetHeight(InTargetActor->GetActorLocation());
	CurrentAcceptanceRadius = (CustomAcceptanceRadius > 0.0f) ? CustomAcceptanceRadius : AcceptanceRadius;
	bIsMovingToTarget = true;

	// Reset stuck detection for new movement
	StuckCheckTime = 0.0f;
}

void UFlyingAIMovementComponent::StopMovement()
{
	bIsMovingToTarget = false;
	TargetActor.Reset();

	if (MovementComponent)
	{
		MovementComponent->StopMovementImmediately();
	}
}

bool UFlyingAIMovementComponent::StartDash(const FVector& Direction)
{
	if (!CharacterOwner || bIsDashing || IsDashOnCooldown())
	{
		return false;
	}

	// Normalize direction
	DashDirection = Direction.GetSafeNormal();
	if (DashDirection.IsNearlyZero())
	{
		return false;
	}

	// Calculate start and end positions for interpolation
	DashStartPosition = CharacterOwner->GetActorLocation();

	// Calculate dash distance based on speed and duration
	const float DashDistance = DashSpeed * DashDuration;
	DashTargetPosition = DashStartPosition + DashDirection * DashDistance;

	// Validate target position height
	DashTargetPosition = ValidateTargetHeight(DashTargetPosition);

	bIsDashing = true;
	DashElapsedTime = 0.0f;

	// Stop any current movement
	if (MovementComponent)
	{
		MovementComponent->StopMovementImmediately();
		MovementComponent->Velocity = FVector::ZeroVector;
	}

	UE_LOG(LogTemp, Warning, TEXT("StartDash: From (%.0f,%.0f,%.0f) to (%.0f,%.0f,%.0f), Distance=%.0f"),
		DashStartPosition.X, DashStartPosition.Y, DashStartPosition.Z,
		DashTargetPosition.X, DashTargetPosition.Y, DashTargetPosition.Z,
		DashDistance);

	return true;
}

bool UFlyingAIMovementComponent::StartEvasiveDash(const FVector& ThreatLocation)
{
	if (!CharacterOwner)
	{
		return false;
	}

	const FVector CurrentLocation = CharacterOwner->GetActorLocation();
	const FVector ToThreat = (ThreatLocation - CurrentLocation).GetSafeNormal();

	// Calculate perpendicular directions (left/right relative to threat)
	const FVector RightDir = FVector::CrossProduct(ToThreat, FVector::UpVector).GetSafeNormal();

	// Randomly choose left or right
	FVector HorizontalDir = FMath::RandBool() ? RightDir : -RightDir;

	// Add vertical component (randomly up or down, biased towards current height bounds)
	float VerticalComponent = FMath::RandRange(-0.5f, 0.5f);

	// Bias vertical direction based on current height
	const float CurrentHeight = GetHeightAboveGround(CurrentLocation);
	const float HeightMidpoint = (MinHoverHeight + MaxHoverHeight) * 0.5f;

	if (CurrentHeight > HeightMidpoint)
	{
		// Above midpoint, bias downward
		VerticalComponent -= 0.3f;
	}
	else
	{
		// Below midpoint, bias upward
		VerticalComponent += 0.3f;
	}

	// Combine horizontal and vertical
	FVector DashDir = HorizontalDir + FVector::UpVector * VerticalComponent;
	DashDir.Normalize();

	return StartDash(DashDir);
}

// ==================== Patrol & Point Generation ====================

bool UFlyingAIMovementComponent::GetRandomPatrolPoint(FVector& OutPoint)
{
	const FVector Center = bPatrolAroundSpawn ? SpawnLocation : CharacterOwner->GetActorLocation();
	return GetRandomPointInVolume(Center, PatrolRadius, MinHoverHeight, MaxHoverHeight, OutPoint);
}

bool UFlyingAIMovementComponent::GetRandomPointInVolume(const FVector& Center, float HorizontalRadius, float MinHeight, float MaxHeight, FVector& OutPoint)
{
	if (!GetWorld())
	{
		return false;
	}

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(GetOwner());

	// Try multiple times to find a valid NavMesh-projected point
	const int32 MaxAttempts = 10;

	for (int32 Attempt = 0; Attempt < MaxAttempts; ++Attempt)
	{
		// Generate random point on XY plane
		const float RandomAngle = FMath::RandRange(0.0f, 2.0f * PI);
		const float RandomRadius = FMath::RandRange(0.0f, HorizontalRadius);

		FVector GroundPoint = Center;
		GroundPoint.X += FMath::Cos(RandomAngle) * RandomRadius;
		GroundPoint.Y += FMath::Sin(RandomAngle) * RandomRadius;

		// Check NavMesh projection
		FVector ProjectedPoint;
		if (!ProjectToNavMesh(GroundPoint, ProjectedPoint))
		{
			// Not on NavMesh, try again
			continue;
		}

		// Find ground height at projected XY position - trace DOWN from center to find floor
		FHitResult GroundHit;
		FVector TraceStart = FVector(ProjectedPoint.X, ProjectedPoint.Y, Center.Z + 100.0f);
		FVector TraceEnd = FVector(ProjectedPoint.X, ProjectedPoint.Y, Center.Z - 10000.0f);

		float GroundZ = Center.Z - DefaultHoverHeight; // Fallback

		// Trace down to find floor (surface facing up)
		if (GetWorld()->LineTraceSingleByChannel(GroundHit, TraceStart, TraceEnd, ECC_WorldStatic, QueryParams))
		{
			if (GroundHit.ImpactNormal.Z > 0.7f) // Floor faces up
			{
				GroundZ = GroundHit.ImpactPoint.Z;
			}
		}

		// Find ceiling height - trace UP from ground
		FHitResult CeilingHit;
		const FVector CeilingTraceStart = FVector(ProjectedPoint.X, ProjectedPoint.Y, GroundZ + 10.0f);
		const FVector CeilingTraceEnd = FVector(ProjectedPoint.X, ProjectedPoint.Y, GroundZ + 10000.0f);

		float MaxAllowedHeight = MaxHeight;
		float ActualMinHeight = MinHeight;

		// Trace up to find ceiling (surface facing down)
		if (GetWorld()->LineTraceSingleByChannel(CeilingHit, CeilingTraceStart, CeilingTraceEnd, ECC_WorldStatic, QueryParams))
		{
			if (CeilingHit.ImpactNormal.Z < -0.7f) // Ceiling faces down
			{
				const float CeilingHeightAboveGround = CeilingHit.ImpactPoint.Z - GroundZ;
				const float CeilingLimitedHeight = CeilingHeightAboveGround - CeilingClearance;
				MaxAllowedHeight = FMath::Min(MaxHeight, CeilingLimitedHeight);

				// If ceiling is very low, adjust min height to fit
				if (CeilingLimitedHeight < MinHeight)
				{
					ActualMinHeight = FMath::Max(50.0f, CeilingLimitedHeight * 0.5f);
					MaxAllowedHeight = FMath::Max(ActualMinHeight, CeilingLimitedHeight);
				}
			}
		}

		// Ensure valid range exists
		if (MaxAllowedHeight < ActualMinHeight)
		{
			// Not enough vertical space even with adjusted limits, try again
			continue;
		}

		// Generate random height within bounds
		const float RandomHeight = FMath::RandRange(ActualMinHeight, MaxAllowedHeight);

		OutPoint = FVector(ProjectedPoint.X, ProjectedPoint.Y, GroundZ + RandomHeight);

		// Validate the point is not inside geometry
		FHitResult ObstacleHit;
		if (GetWorld()->LineTraceSingleByChannel(ObstacleHit, Center, OutPoint, ObstacleChannel, QueryParams))
		{
			// Point is blocked, try to find a valid point along the line
			OutPoint = ObstacleHit.ImpactPoint - (OutPoint - Center).GetSafeNormal() * 100.0f;
		}

		return true;
	}

	// Failed to find valid point - fall back to current location with adjusted height
	OutPoint = ValidateTargetHeight(Center);
	return false;
}

// ==================== State Queries ====================

bool UFlyingAIMovementComponent::IsDashOnCooldown() const
{
	if (!GetWorld())
	{
		return false;
	}

	const float CurrentTime = GetWorld()->GetTimeSeconds();
	return (CurrentTime - LastDashEndTime) < DashCooldown;
}

// ==================== Internal Methods ====================

void UFlyingAIMovementComponent::UpdateMovement(float DeltaTime)
{
	if (!CharacterOwner || !MovementComponent)
	{
		return;
	}

	// Update target if following an actor
	if (TargetActor.IsValid())
	{
		CurrentTargetLocation = ValidateTargetHeight(TargetActor->GetActorLocation());
	}

	const FVector CurrentLocation = CharacterOwner->GetActorLocation();
	FVector ToTarget = CurrentTargetLocation - CurrentLocation;
	const float DistanceToTarget = ToTarget.Size();

	// Check if we've reached the target
	if (DistanceToTarget <= CurrentAcceptanceRadius)
	{
		StuckCheckTime = 0.0f;
		CompleteMovement(true);
		return;
	}

	// Stuck detection: if drone hasn't moved significantly in StuckTimeThreshold, abort movement
	const float CurrentTime = GetWorld()->GetTimeSeconds();
	if (StuckCheckTime <= 0.0f)
	{
		// Start tracking
		StuckCheckPosition = CurrentLocation;
		StuckCheckTime = CurrentTime;
	}
	else if (CurrentTime - StuckCheckTime >= StuckTimeThreshold)
	{
		const float DistanceMoved = FVector::Dist(CurrentLocation, StuckCheckPosition);
		if (DistanceMoved < StuckDistanceThreshold)
		{
			// Drone is stuck - abort movement so StateTree picks a new destination
			StuckCheckTime = 0.0f;
			CompleteMovement(false);
			return;
		}

		// Reset stuck check window
		StuckCheckPosition = CurrentLocation;
		StuckCheckTime = CurrentTime;
	}

	// Calculate desired direction
	FVector DesiredDirection = ToTarget.GetSafeNormal();

	// Apply obstacle avoidance if enabled
	if (bEnableObstacleAvoidance)
	{
		DesiredDirection = GetAvoidanceAdjustedDirection(DesiredDirection);
	}

	// Apply movement
	ApplyMovementInput(DesiredDirection, FlySpeed);
}

void UFlyingAIMovementComponent::UpdateDash(float DeltaTime)
{
	if (!CharacterOwner || !MovementComponent)
	{
		UE_LOG(LogTemp, Warning, TEXT("UpdateDash: Missing owner or movement component"));
		CompleteDash();
		return;
	}

	// Update elapsed time
	DashElapsedTime += DeltaTime;

	// Check if dash is complete
	if (DashElapsedTime >= DashDuration)
	{
		UE_LOG(LogTemp, Warning, TEXT("UpdateDash: Dash complete after %.2f seconds"), DashElapsedTime);
		CompleteDash();
		return;
	}

	// Calculate interpolation alpha
	float Alpha = FMath::Clamp(DashElapsedTime / DashDuration, 0.0f, 1.0f);

	// Ease-out for smooth finish (like MeleeNPC knockback)
	float EasedAlpha;
	if (Alpha < 0.9f)
	{
		EasedAlpha = Alpha;
	}
	else
	{
		float LastSegmentAlpha = (Alpha - 0.9f) / 0.1f;
		float EasedSegment = FMath::InterpEaseOut(0.0f, 0.1f, LastSegmentAlpha, 2.0f);
		EasedAlpha = 0.9f + EasedSegment;
	}

	// Calculate next position via interpolation
	FVector CurrentPos = CharacterOwner->GetActorLocation();
	FVector NextPos = FMath::Lerp(DashStartPosition, DashTargetPosition, EasedAlpha);

	// Collision check along path
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(CharacterOwner);
	FHitResult Hit;

	bool bBlocked = GetWorld()->SweepSingleByChannel(
		Hit,
		CurrentPos,
		NextPos,
		FQuat::Identity,
		ECC_Pawn,
		FCollisionShape::MakeSphere(50.0f), // Approximate drone radius
		QueryParams
	);

	if (bBlocked && Hit.bBlockingHit)
	{
		// Hit obstacle - stop at hit location
		NextPos = Hit.Location;
		UE_LOG(LogTemp, Warning, TEXT("UpdateDash: Blocked by %s"),
			Hit.GetActor() ? *Hit.GetActor()->GetName() : TEXT("World"));
		CompleteDash();
		return;
	}

	// Move drone via SetActorLocation (like MeleeNPC)
	bool bMoved = CharacterOwner->SetActorLocation(NextPos, true);

	UE_LOG(LogTemp, Warning, TEXT("UpdateDash: Alpha=%.2f, From (%.0f,%.0f,%.0f) To (%.0f,%.0f,%.0f), Moved=%s"),
		EasedAlpha, CurrentPos.X, CurrentPos.Y, CurrentPos.Z,
		NextPos.X, NextPos.Y, NextPos.Z, bMoved ? TEXT("YES") : TEXT("NO"));

	// Update velocity for visuals/animations
	FVector FrameVelocity = (NextPos - CurrentPos) / DeltaTime;
	MovementComponent->Velocity = FrameVelocity;
}

void UFlyingAIMovementComponent::ApplyHoverOscillation(float DeltaTime)
{
	if (!MovementComponent)
	{
		return;
	}

	OscillationTime += DeltaTime;

	// Calculate oscillation offset
	const float OscillationOffset = FMath::Sin(OscillationTime * HoverOscillationFrequency * 2.0f * PI) * HoverOscillationAmplitude;

	// Apply as vertical velocity adjustment
	const float TargetVerticalVelocity = OscillationOffset * HoverOscillationFrequency * 2.0f * PI;

	// Blend with current velocity
	FVector CurrentVelocity = MovementComponent->Velocity;
	CurrentVelocity.Z = FMath::FInterpTo(CurrentVelocity.Z, TargetVerticalVelocity, DeltaTime, 5.0f);
	MovementComponent->Velocity = CurrentVelocity;
}

FVector UFlyingAIMovementComponent::GetAvoidanceAdjustedDirection(const FVector& DesiredDirection)
{
	if (!CharacterOwner || !GetWorld())
	{
		return DesiredDirection;
	}

	const FVector CurrentLocation = CharacterOwner->GetActorLocation();

	FHitResult Hit;
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(CharacterOwner);

	const FVector TraceEnd = CurrentLocation + DesiredDirection * ObstacleCheckDistance;

	if (GetWorld()->LineTraceSingleByChannel(Hit, CurrentLocation, TraceEnd, ObstacleChannel, QueryParams))
	{
		// Obstacle detected, calculate avoidance direction
		const FVector ObstacleNormal = Hit.ImpactNormal;

		// Reflect direction off obstacle
		FVector AvoidanceDirection = FMath::GetReflectionVector(DesiredDirection, ObstacleNormal);

		// Blend between desired and avoidance based on proximity
		const float ProximityFactor = 1.0f - (Hit.Distance / ObstacleCheckDistance);
		return FMath::Lerp(DesiredDirection, AvoidanceDirection, ProximityFactor).GetSafeNormal();
	}

	return DesiredDirection;
}

float UFlyingAIMovementComponent::GetHeightAboveGround(const FVector& Location) const
{
	if (!GetWorld())
	{
		return DefaultHoverHeight;
	}

	FHitResult Hit;
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(GetOwner());

	// Trace DOWN to find floor
	const FVector TraceStart = Location;
	const FVector TraceEnd = Location - FVector(0.0f, 0.0f, 10000.0f);

	if (GetWorld()->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_WorldStatic, QueryParams))
	{
		// Only count as floor if surface faces up
		if (Hit.ImpactNormal.Z > 0.7f)
		{
			return Location.Z - Hit.ImpactPoint.Z;
		}
	}

	return DefaultHoverHeight;
}

FVector UFlyingAIMovementComponent::ValidateTargetHeight(const FVector& TargetLocation) const
{
	if (!GetWorld())
	{
		return TargetLocation;
	}

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(GetOwner());

	// Find ground (floor) by tracing DOWN from drone's current position
	// This way we find the floor below us, not the ceiling above
	FHitResult GroundHit;

	// Start from current drone position (or target location) and trace DOWN
	float StartZ = TargetLocation.Z;
	if (CharacterOwner)
	{
		// Use drone's current Z as starting point for downward trace
		StartZ = FMath::Max(TargetLocation.Z, CharacterOwner->GetActorLocation().Z);
	}

	const FVector GroundTraceStart = FVector(TargetLocation.X, TargetLocation.Y, StartZ + 100.0f);
	const FVector GroundTraceEnd = FVector(TargetLocation.X, TargetLocation.Y, StartZ - 10000.0f);

	float GroundZ = TargetLocation.Z - DefaultHoverHeight;

	// Trace DOWN to find the floor
	if (GetWorld()->LineTraceSingleByChannel(GroundHit, GroundTraceStart, GroundTraceEnd, ECC_WorldStatic, QueryParams))
	{
		// Only accept surfaces facing UP (floors, not ceilings or walls)
		if (GroundHit.ImpactNormal.Z > 0.7f) // Surface is mostly horizontal and facing up
		{
			GroundZ = GroundHit.ImpactPoint.Z;
		}
		else
		{
			// Hit a wall or ceiling while tracing down - unusual, use fallback
			UE_LOG(LogTemp, Warning, TEXT("ValidateTargetHeight: Downward trace hit non-floor surface (Normal Z=%.2f)"), GroundHit.ImpactNormal.Z);
		}
	}

	// Find ceiling height at target XY (trace upward from ground)
	FHitResult CeilingHit;
	const FVector CeilingTraceStart = FVector(TargetLocation.X, TargetLocation.Y, GroundZ + 10.0f);
	const FVector CeilingTraceEnd = FVector(TargetLocation.X, TargetLocation.Y, GroundZ + 10000.0f);

	float MaxAllowedHeight = MaxHoverHeight;
	float ActualMinHeight = MinHoverHeight;

	// Trace UP to find ceiling - use WorldStatic to hit actual geometry
	if (GetWorld()->LineTraceSingleByChannel(CeilingHit, CeilingTraceStart, CeilingTraceEnd, ECC_WorldStatic, QueryParams))
	{
		// Only count surfaces facing DOWN as ceilings
		if (CeilingHit.ImpactNormal.Z < -0.7f)
		{
			// Ceiling found - limit max height to ceiling minus clearance
			const float CeilingHeightAboveGround = CeilingHit.ImpactPoint.Z - GroundZ;
			const float CeilingLimitedHeight = CeilingHeightAboveGround - CeilingClearance;

			// If ceiling is very low, we must respect it even if below MinHoverHeight
			MaxAllowedHeight = FMath::Min(MaxHoverHeight, CeilingLimitedHeight);

			// Also adjust min height if ceiling is very low
			if (CeilingLimitedHeight < MinHoverHeight)
			{
				// Ceiling is too low - hover at half the available space
				ActualMinHeight = FMath::Max(50.0f, CeilingLimitedHeight * 0.5f);
				MaxAllowedHeight = FMath::Max(ActualMinHeight, CeilingLimitedHeight);
			}
		}
	}

	// Calculate desired height above ground
	float DesiredHeight = TargetLocation.Z - GroundZ;

	// Clamp to valid range (considering ceiling)
	DesiredHeight = FMath::Clamp(DesiredHeight, ActualMinHeight, MaxAllowedHeight);

	return FVector(TargetLocation.X, TargetLocation.Y, GroundZ + DesiredHeight);
}

void UFlyingAIMovementComponent::ApplyMovementInput(const FVector& Direction, float Speed)
{
	if (!CharacterOwner || !MovementComponent)
	{
		return;
	}

	// Update max speed
	MovementComponent->MaxFlySpeed = Speed;

	// Get collision-safe direction
	const float DeltaTime = GetWorld()->GetDeltaSeconds();
	FVector SafeDirection = GetCollisionSafeDirection(Direction, Speed, DeltaTime);

	// If all directions are blocked, stop
	if (SafeDirection.IsNearlyZero())
	{
		MovementComponent->StopMovementImmediately();
		return;
	}

	// Check if we have a controller
	if (CharacterOwner->GetController())
	{
		// Use standard movement input (works with AI controller)
		CharacterOwner->AddMovementInput(SafeDirection, 1.0f);
	}
	else
	{
		// No controller - apply velocity directly
		FVector TargetVelocity = SafeDirection * Speed;
		FVector CurrentVelocity = MovementComponent->Velocity;

		// Interpolate towards target velocity
		FVector NewVelocity = FMath::VInterpTo(CurrentVelocity, TargetVelocity, DeltaTime, FlyAcceleration / Speed);
		MovementComponent->Velocity = NewVelocity;
	}
}

void UFlyingAIMovementComponent::CompleteMovement(bool bSuccess)
{
	bIsMovingToTarget = false;
	TargetActor.Reset();

	// Broadcast completion
	OnMovementCompleted.Broadcast(bSuccess);
}

void UFlyingAIMovementComponent::CompleteDash()
{
	bIsDashing = false;
	LastDashEndTime = GetWorld()->GetTimeSeconds();

	// Restore normal speed
	if (MovementComponent)
	{
		MovementComponent->MaxFlySpeed = FlySpeed;
	}

	// Broadcast completion
	OnDashCompleted.Broadcast();
}

FVector UFlyingAIMovementComponent::GetCollisionSafeDirection(const FVector& DesiredDirection, float Speed, float DeltaTime) const
{
	if (!CharacterOwner || !GetWorld())
	{
		return DesiredDirection;
	}

	const UCapsuleComponent* Capsule = CharacterOwner->GetCapsuleComponent();
	if (!Capsule)
	{
		return DesiredDirection;
	}

	const FVector CurrentLocation = CharacterOwner->GetActorLocation();
	// Check further ahead to prevent tunneling at high speeds
	const float CheckDistance = FMath::Max(Speed * DeltaTime * 2.0f, 50.0f);
	const FVector TraceEnd = CurrentLocation + DesiredDirection * CheckDistance;

	FHitResult Hit;
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(CharacterOwner);

	// Use capsule sweep for accurate collision detection
	const float CapsuleRadius = Capsule->GetScaledCapsuleRadius();
	const float CapsuleHalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const FCollisionShape CapsuleShape = FCollisionShape::MakeCapsule(CapsuleRadius, CapsuleHalfHeight);

	if (GetWorld()->SweepSingleByChannel(Hit, CurrentLocation, TraceEnd, CharacterOwner->GetActorQuat(), ECC_Pawn, CapsuleShape, QueryParams))
	{
		// We would hit something - slide along the surface
		const FVector ImpactNormal = Hit.ImpactNormal;

		// Calculate slide direction (project desired direction onto the plane defined by impact normal)
		FVector SlideDirection = FVector::VectorPlaneProject(DesiredDirection, ImpactNormal);

		if (!SlideDirection.IsNearlyZero())
		{
			SlideDirection.Normalize();

			// Check if slide direction is also blocked
			const FVector SlideEnd = CurrentLocation + SlideDirection * CheckDistance;
			FHitResult SlideHit;

			if (!GetWorld()->SweepSingleByChannel(SlideHit, CurrentLocation, SlideEnd, CharacterOwner->GetActorQuat(), ECC_Pawn, CapsuleShape, QueryParams))
			{
				return SlideDirection;
			}

			// Both directions blocked - try to find any safe direction
			// Try perpendicular directions
			const FVector RightDir = FVector::CrossProduct(DesiredDirection, FVector::UpVector).GetSafeNormal();
			const FVector LeftDir = -RightDir;
			const FVector UpDir = FVector::UpVector;
			const FVector DownDir = -FVector::UpVector;

			TArray<FVector> AlternativeDirections = { RightDir, LeftDir, UpDir, DownDir };

			for (const FVector& AltDir : AlternativeDirections)
			{
				const FVector AltEnd = CurrentLocation + AltDir * CheckDistance;
				FHitResult AltHit;

				if (!GetWorld()->SweepSingleByChannel(AltHit, CurrentLocation, AltEnd, CharacterOwner->GetActorQuat(), ECC_Pawn, CapsuleShape, QueryParams))
				{
					return AltDir;
				}
			}

			// All directions blocked - stop movement
			return FVector::ZeroVector;
		}

		// Slide direction is zero (hitting head-on) - push back along normal
		return ImpactNormal;
	}

	// No collision - use desired direction
	return DesiredDirection;
}

float UFlyingAIMovementComponent::GetHeightToCeiling(const FVector& Location) const
{
	if (!GetWorld())
	{
		return MAX_FLT;
	}

	FHitResult Hit;
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(GetOwner());

	// Trace UP to find ceiling
	const FVector TraceStart = Location;
	const FVector TraceEnd = Location + FVector(0.0f, 0.0f, 10000.0f);

	if (GetWorld()->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_WorldStatic, QueryParams))
	{
		// Only count as ceiling if surface faces down
		if (Hit.ImpactNormal.Z < -0.7f)
		{
			return Hit.Distance;
		}
	}

	return MAX_FLT;
}

bool UFlyingAIMovementComponent::ProjectToNavMesh(const FVector& Location, FVector& OutProjectedLocation) const
{
	if (!bRequireNavMeshProjection)
	{
		OutProjectedLocation = Location;
		return true;
	}

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!NavSys)
	{
		// No navigation system - allow movement anyway
		OutProjectedLocation = Location;
		return true;
	}

	// Project XY position to NavMesh (ignore Z for flying units)
	FNavLocation NavLocation;
	const FVector ProjectionExtent(NavMeshProjectionRadius, NavMeshProjectionRadius, 10000.0f);

	if (NavSys->ProjectPointToNavigation(Location, NavLocation, ProjectionExtent))
	{
		// Keep original Z, just validate XY is over NavMesh
		OutProjectedLocation = FVector(NavLocation.Location.X, NavLocation.Location.Y, Location.Z);
		return true;
	}

	return false;
}