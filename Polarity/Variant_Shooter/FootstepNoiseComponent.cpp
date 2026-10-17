// FootstepNoiseComponent.cpp

#include "Variant_Shooter/FootstepNoiseComponent.h"

#include "ApexMovementComponent.h"
#include "Coop/CoopPlayers.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Variant_Shooter/AI/ShooterAIController.h"
#include "Variant_Shooter/AI/ShooterNPC.h"

UFootstepNoiseComponent::UFootstepNoiseComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickInterval = 0.05f;
}

void UFootstepNoiseComponent::BeginPlay()
{
	Super::BeginPlay();

	// The soldiers hear on the server only, and only players are listened for.
	if (!GetOwner() || !GetOwner()->HasAuthority() || GetOwner()->IsA<AShooterNPC>())
	{
		SetComponentTickEnabled(false);
	}
}

void UFootstepNoiseComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	ACharacter* const Body = Cast<ACharacter>(GetOwner());
	const UWorld* const World = GetWorld();
	const UCharacterMovementComponent* const Movement = Body ? Body->GetCharacterMovement() : nullptr;

	// Players only: the soldiers' own feet are not part of this.
	if (!Movement || !World || !CoopPlayers::IsPlayer(Body))
	{
		return;
	}
	const float Now = World->GetTimeSeconds();
	const FVector Velocity = Movement->Velocity;

	// A hard landing: heard once, when it happens.
	if (Movement->IsFalling())
	{
		bWasFalling = true;
		FallSpeed = FMath::Max(FallSpeed, static_cast<float>(-Velocity.Z));
		return;
	}
	if (bWasFalling)
	{
		bWasFalling = false;
		if (FallSpeed >= HardLandingSpeed && LandingRange > 0.0f)
		{
			AShooterAIController::ReportMovementNoise(this, Body, Body->GetActorLocation(), LandingRange);
			NextNoiseTime = Now + Interval;
		}
		FallSpeed = 0.0f;
	}

	if (Now < NextNoiseTime)
	{
		return;
	}

	const UApexMovementComponent* const Apex = Cast<UApexMovementComponent>(Movement);
	const float Speed = static_cast<float>(Velocity.Size2D());
	float Range = 0.0f;
	if (Apex && Apex->IsSliding())
	{
		Range = SlideRange;
	}
	else if (Apex && Apex->IsWallRunning())
	{
		Range = WallRunRange;
	}
	else if (Movement->IsMovingOnGround() && !Movement->IsCrouching() && Speed >= RunSpeed)
	{
		Range = RunRange;
	}
	else if (Movement->IsMovingOnGround() && !Movement->IsCrouching() && Speed > 50.0f)
	{
		Range = WalkRange;
	}

	if (Range > 0.0f)
	{
		AShooterAIController::ReportMovementNoise(this, Body, Body->GetActorLocation(), Range);
		NextNoiseTime = Now + Interval;
	}
}
