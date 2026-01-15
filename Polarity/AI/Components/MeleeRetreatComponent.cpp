// MeleeRetreatComponent.cpp

#include "MeleeRetreatComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NavigationSystem.h"
#include "AIController.h"
#include "Kismet/GameplayStatics.h"

UMeleeRetreatComponent::UMeleeRetreatComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UMeleeRetreatComponent::BeginPlay()
{
	Super::BeginPlay();

	// Cache owner references
	OwnerCharacter = Cast<ACharacter>(GetOwner());
	if (OwnerCharacter)
	{
		MovementComponent = OwnerCharacter->GetCharacterMovement();
		if (MovementComponent)
		{
			OriginalMaxWalkSpeed = MovementComponent->MaxWalkSpeed;
		}
	}
}

void UMeleeRetreatComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Update cooldown
	if (CooldownRemaining > 0.0f)
	{
		CooldownRemaining -= DeltaTime;
	}

	// Update retreat state
	if (bIsRetreating)
	{
		RetreatTimeRemaining -= DeltaTime;
		
		if (RetreatTimeRemaining <= 0.0f)
		{
			EndRetreat();
		}
	}
	else
	{
		// Check proximity trigger when not retreating
		UpdateProximityTrigger(DeltaTime);
	}
}

bool UMeleeRetreatComponent::TriggerRetreat(AActor* Attacker)
{
	if (!CanRetreat() || !Attacker || !OwnerCharacter)
	{
		return false;
	}

	LastAttacker = Attacker;
	
	// Calculate retreat direction
	RetreatDirection = CalculateRetreatDirection(Attacker);
	
	// Find valid destination
	RetreatDestination = FindRetreatDestination(RetreatDirection);

	// Start retreat
	bIsRetreating = true;
	RetreatTimeRemaining = RetreatDuration;
	CooldownRemaining = RetreatCooldown + RetreatDuration;

	// Apply speed boost
	ApplyRetreatSpeed();

	// Notify listeners
	OnRetreatStarted.Broadcast(RetreatDirection);

	// Command AI to move to retreat destination
	if (AAIController* AIController = Cast<AAIController>(OwnerCharacter->GetController()))
	{
		AIController->MoveToLocation(RetreatDestination, 50.0f, true, true, false, true);
	}

	return true;
}

void UMeleeRetreatComponent::EndRetreat()
{
	if (!bIsRetreating)
	{
		return;
	}

	bIsRetreating = false;
	RetreatTimeRemaining = 0.0f;
	RetreatDirection = FVector::ZeroVector;

	// Restore speed
	RestoreOriginalSpeed();

	// Notify listeners
	OnRetreatEnded.Broadcast();
}

bool UMeleeRetreatComponent::CanRetreat() const
{
	return !bIsRetreating && CooldownRemaining <= 0.0f;
}

FVector UMeleeRetreatComponent::CalculateRetreatDirection(AActor* Attacker) const
{
	if (!Attacker || !OwnerCharacter)
	{
		return FVector::BackwardVector;
	}

	// Direction away from attacker (2D, ignore Z)
	FVector Direction = OwnerCharacter->GetActorLocation() - Attacker->GetActorLocation();
	Direction.Z = 0.0f;
	
	if (Direction.IsNearlyZero())
	{
		// Attacker is at same position, retreat backward
		return -OwnerCharacter->GetActorForwardVector();
	}

	return Direction.GetSafeNormal();
}

FVector UMeleeRetreatComponent::FindRetreatDestination(const FVector& Direction) const
{
	if (!OwnerCharacter)
	{
		return FVector::ZeroVector;
	}

	const FVector StartLocation = OwnerCharacter->GetActorLocation();
	
	// Try direct retreat first
	FVector DirectDestination = StartLocation + (Direction * RetreatDistance);
	if (IsPointReachable(DirectDestination))
	{
		return DirectDestination;
	}

	// Try alternative directions
	for (int32 i = 0; i < AlternativeDirectionCount; ++i)
	{
		// Alternate between positive and negative angles
		const float AngleOffset = PathDeviationAngle * ((i / 2) + 1) * (i % 2 == 0 ? 1.0f : -1.0f);
		const FVector RotatedDirection = Direction.RotateAngleAxis(AngleOffset, FVector::UpVector);
		const FVector AlternativeDestination = StartLocation + (RotatedDirection * RetreatDistance);

		if (IsPointReachable(AlternativeDestination))
		{
			return AlternativeDestination;
		}
	}

	// Fallback: try half distance in direct direction
	FVector HalfDestination = StartLocation + (Direction * RetreatDistance * 0.5f);
	if (IsPointReachable(HalfDestination))
	{
		return HalfDestination;
	}

	// Last resort: stay in place
	return StartLocation;
}

bool UMeleeRetreatComponent::IsPointReachable(const FVector& Point) const
{
	UNavigationSystemV1* NavSystem = UNavigationSystemV1::GetCurrent(GetWorld());
	if (!NavSystem)
	{
		return true; // No nav system, assume reachable
	}

	FNavLocation NavLocation;
	const FVector QueryExtent(100.0f, 100.0f, 250.0f);
	
	return NavSystem->ProjectPointToNavigation(Point, NavLocation, QueryExtent);
}

void UMeleeRetreatComponent::ApplyRetreatSpeed()
{
	if (MovementComponent)
	{
		MovementComponent->MaxWalkSpeed = OriginalMaxWalkSpeed * RetreatSpeedMultiplier;
	}
}

void UMeleeRetreatComponent::RestoreOriginalSpeed()
{
	if (MovementComponent)
	{
		MovementComponent->MaxWalkSpeed = OriginalMaxWalkSpeed;
	}
}

void UMeleeRetreatComponent::SetProximityTarget(AActor* Target)
{
	ProximityTarget = Target;
}

void UMeleeRetreatComponent::UpdateProximityTrigger(float DeltaTime)
{
	if (!bEnableProximityTrigger || !CanRetreat() || !OwnerCharacter)
	{
		return;
	}

	// Find target if not set
	if (!ProximityTarget.IsValid())
	{
		FindProximityTarget();
		if (!ProximityTarget.IsValid())
		{
			return;
		}
	}

	// Check distance to target
	const float Distance = FVector::Dist(
		OwnerCharacter->GetActorLocation(),
		ProximityTarget->GetActorLocation()
	);

	if (Distance <= ProximityTriggerDistance)
	{
		// Within proximity, accumulate time
		ProximityTimeAccumulated += DeltaTime;

		if (ProximityTimeAccumulated >= ProximityTriggerTime)
		{
			// Trigger retreat
			TriggerRetreat(ProximityTarget.Get());
			ProximityTimeAccumulated = 0.0f;
		}
	}
	else
	{
		// Outside proximity, reset timer
		ProximityTimeAccumulated = 0.0f;
	}
}

void UMeleeRetreatComponent::FindProximityTarget()
{
	// Find player pawn as default target
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
	{
		ProximityTarget = PC->GetPawn();
	}
}
