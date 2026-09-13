// BuildableDefinition.cpp

#include "BuildableDefinition.h"

#include "BuildableActor.h"
#include "CollisionQueryParams.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"

FBuildableLevelStats UBuildableDefinition::GetLevelStats(int32 Level) const
{
	if (Levels.Num() == 0)
	{
		return FBuildableLevelStats{};
	}
	return Levels[FMath::Clamp(Level - 1, 0, Levels.Num() - 1)];
}

bool UBuildableDefinition::GetLocalFootprint(FBox& OutBox) const
{
	// A placeholder that still lets the whole loop be played before any art exists.
	OutBox = FBox(FVector(-50.0f, -50.0f, 0.0f), FVector(50.0f, 50.0f, 100.0f));

	const ABuildableActor* const CDO = ActorClass ? ActorClass->GetDefaultObject<ABuildableActor>() : nullptr;
	const UStaticMeshComponent* const Mesh = CDO ? CDO->GetMesh() : nullptr;
	if (!Mesh || !Mesh->GetStaticMesh())
	{
		return false;
	}

	// The mesh's own bounds, carried into actor space by the offset and scale the Blueprint gave the
	// component. Native components keep their Blueprint edits on the class default object, which is
	// why this can be read without spawning anything.
	const FBox MeshBox = Mesh->GetStaticMesh()->GetBoundingBox().TransformBy(Mesh->GetRelativeTransform());
	OutBox = MeshBox.ExpandBy(FVector(PlacementPadding, PlacementPadding, PlacementPadding));
	// No clearance below the base: the building stands on the ground, and a box reaching under it
	// would call the floor an obstacle.
	OutBox.Min.Z = MeshBox.Min.Z;
	return true;
}

EBuildablePlacementResult UBuildableDefinition::ValidatePlacement(const UWorld* World, const FTransform& Transform,
	const TArray<AActor*>& Ignore, const AActor* Builder) const
{
	if (!World)
	{
		return EBuildablePlacementResult::NoGround;
	}

	const FVector Origin = Transform.GetLocation();

	if (Builder)
	{
		// Half again the ghost's reach: the builder kept moving while the request was in flight.
		const float Reach = MaxPlaceDistance * 1.5f;
		if (FVector::DistSquared(Builder->GetActorLocation(), Origin) > Reach * Reach)
		{
			return EBuildablePlacementResult::TooFar;
		}
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(BuildablePlacement), false);
	Params.AddIgnoredActors(Ignore);

	// Ground under the origin. A short probe: the ghost already put the origin on a surface, and
	// the server is confirming that surface exists rather than searching for one.
	FHitResult Ground;
	const FVector ProbeTop = Origin + FVector(0.0f, 0.0f, 30.0f);
	const FVector ProbeBottom = Origin - FVector(0.0f, 0.0f, 60.0f);
	if (!World->LineTraceSingleByChannel(Ground, ProbeTop, ProbeBottom, ECC_Visibility, Params))
	{
		return EBuildablePlacementResult::NoGround;
	}

	const float MinNormalZ = FMath::Cos(FMath::DegreesToRadians(MaxSlopeDegrees));
	if (Ground.ImpactNormal.Z < MinNormalZ)
	{
		return EBuildablePlacementResult::TooSteep;
	}

	// Ground is the world, not another building and not somebody's head. Without this the ghost
	// happily stood on top of a turret: the "floor" it found was the turret, which the overlap
	// below then excused as the floor.
	AActor* const GroundActor = Ground.GetActor();
	if (GroundActor && (GroundActor->IsA<ABuildableActor>() || GroundActor->IsA<APawn>()))
	{
		return EBuildablePlacementResult::Blocked;
	}

	// Anything solid inside the footprint. The ground's own actor is left out: on a slope it would
	// poke into the box and read as an obstacle, and it has already been accepted as the floor.
	FBox Footprint;
	GetLocalFootprint(Footprint);
	if (GroundActor)
	{
		Params.AddIgnoredActor(GroundActor);
	}

	// The box is lifted a hair off the base so a flat floor that is exactly at the origin does not
	// count either, then the padding above still catches a low ceiling.
	Footprint.Min.Z += 2.0f;

	FCollisionObjectQueryParams ObjectTypes;
	ObjectTypes.AddObjectTypesToQuery(ECC_WorldStatic);
	ObjectTypes.AddObjectTypesToQuery(ECC_WorldDynamic);
	ObjectTypes.AddObjectTypesToQuery(ECC_Pawn);
	ObjectTypes.AddObjectTypesToQuery(ECC_PhysicsBody);

	const FVector WorldCenter = Transform.TransformPosition(Footprint.GetCenter());
	const FCollisionShape Box = FCollisionShape::MakeBox(Footprint.GetExtent() * Transform.GetScale3D());
	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(Overlaps, WorldCenter, Transform.GetRotation(), ObjectTypes, Box, Params);
	for (const FOverlapResult& Overlap : Overlaps)
	{
		// Only what would actually stop a player counts: a physical body that blocks pawns. A
		// query-only hitbox, a resting bullet, a pickup's trigger, a ghost: things the player walks
		// through are not in the way of a building either. (A bullet lying on the floor turned the
		// first ghost red, which is how this filter came to be.)
		const UPrimitiveComponent* const Component = Overlap.GetComponent();
		if (!Component || Component->GetCollisionEnabled() != ECollisionEnabled::QueryAndPhysics)
		{
			continue;
		}
		if (Component->GetCollisionResponseToChannel(ECC_Pawn) != ECR_Block)
		{
			continue;
		}
		return EBuildablePlacementResult::Blocked;
	}

	return EBuildablePlacementResult::Valid;
}
