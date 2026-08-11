// Editor-authored adaptive modular stairs for connecting elevated platforms.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AdaptiveStairActor.generated.h"

class UArrowComponent;
class UBoxComponent;
class UHierarchicalInstancedStaticMeshComponent;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;

UENUM(BlueprintType)
enum class EAdaptiveStairFitMode : uint8
{
	/** Actor origin is the upper landing; stairs descend along local +X to TargetFloorZ. */
	TargetWorldZ,

	/** Actor origin is the upper landing; the viewport-editable BottomPointLocal is the exact lower connection. */
	LocalEndpoint
};

/**
 * Straight modular stairs whose actor origin is the upper connection point.
 * In TargetWorldZ mode local +X points toward the lower exit. In LocalEndpoint mode
 * BottomPointLocal also controls the horizontal heading.
 *
 * Blender contract for every modular mesh:
 * - forward/progression axis: +X (from upper landing toward lower landing)
 * - width axis: +Y
 * - up axis: +Z
 * - step/rail module pivot: upper connection plane at X=0, Z=0
 * - one authored module advances AuthoredTreadDepth along +X and descends
 *   AuthoredRiserHeight along -Z
 * - cap pivots sit on their connection planes
 * - intentional overlap past a repeat boundary is allowed; repetition uses the
 *   declared authored dimensions, never mesh bounds
 */
UCLASS(BlueprintType)
class POLARITY_API AAdaptiveStairActor : public AActor
{
	GENERATED_BODY()

public:
	AAdaptiveStairActor();
	virtual void OnConstruction(const FTransform& Transform) override;

	/** Rebuild modules, collision, validation data, and editor preview. */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Adaptive Stairs")
	void RegenerateStairs();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adaptive Stairs|Shape")
	EAdaptiveStairFitMode FitMode = EAdaptiveStairFitMode::TargetWorldZ;

	/** Absolute world-space height of the lower connection in TargetWorldZ mode. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adaptive Stairs|Shape", meta = (EditCondition = "FitMode == EAdaptiveStairFitMode::TargetWorldZ", Units = "cm"))
	float TargetFloorZ = 0.f;

	/** Exact lower connection relative to the actor. Drag its viewport widget for unusual spans. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adaptive Stairs|Shape", meta = (EditCondition = "FitMode == EAdaptiveStairFitMode::LocalEndpoint", MakeEditWidget = true, Units = "cm"))
	FVector BottomPointLocal = FVector(1500.f, 0.f, -700.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adaptive Stairs|Shape", meta = (ClampMin = "50.0", UIMin = "200.0", UIMax = "2000.0", Units = "cm"))
	float Width = 600.f;

	/** Preferred horizontal advance of one step. Exact in TargetWorldZ mode. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adaptive Stairs|Shape", meta = (ClampMin = "10.0", UIMin = "30.0", UIMax = "150.0", Units = "cm"))
	float DesiredTreadDepth = 60.f;

	/** Step count is increased as needed so the generated riser does not exceed this value. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adaptive Stairs|Shape", meta = (ClampMin = "1.0", UIMin = "10.0", UIMax = "50.0", Units = "cm"))
	float MaxRiserHeight = 30.f;

	/** Validation limit. Endpoint mode stays exact but reports a warning below this value. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adaptive Stairs|Validation", meta = (ClampMin = "1.0", Units = "cm"))
	float MinTreadDepth = 30.f;

	/** Validation limit. Geometry remains exact so the designer can correct the endpoint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adaptive Stairs|Validation", meta = (ClampMin = "1.0", ClampMax = "80.0", Units = "deg"))
	float MaxSlopeAngle = 45.f;

	/** Safety cap against accidentally creating thousands of editor instances. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adaptive Stairs|Validation", AdvancedDisplay, meta = (ClampMin = "1", ClampMax = "1024"))
	int32 MaxGeneratedSteps = 256;

	/** Dimensions used when the repeatable Blender modules were authored. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adaptive Stairs|Modular", meta = (ClampMin = "1.0", Units = "cm"))
	float AuthoredTreadDepth = 60.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adaptive Stairs|Modular", meta = (ClampMin = "1.0", Units = "cm"))
	float AuthoredRiserHeight = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adaptive Stairs|Modular", meta = (ClampMin = "10.0", Units = "cm"))
	float AuthoredModuleWidth = 600.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adaptive Stairs|Modular")
	TObjectPtr<UStaticMesh> StepSegmentMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adaptive Stairs|Modular")
	TObjectPtr<UStaticMesh> LeftRailSegmentMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adaptive Stairs|Modular")
	TObjectPtr<UStaticMesh> RightRailSegmentMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adaptive Stairs|Modular")
	TObjectPtr<UStaticMesh> TopCapMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adaptive Stairs|Modular")
	TObjectPtr<UStaticMesh> BottomCapMesh;

	/** Fine alignment for imported modules. Offsets are expressed in module-local axes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adaptive Stairs|Modular|Offsets")
	FVector StepSegmentOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adaptive Stairs|Modular|Offsets")
	FVector LeftRailSegmentOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adaptive Stairs|Modular|Offsets")
	FVector RightRailSegmentOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adaptive Stairs|Modular|Offsets")
	FVector TopCapOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adaptive Stairs|Modular|Offsets")
	FVector BottomCapOffset = FVector::ZeroVector;

	/** Visual steps stay non-colliding; this creates a smooth playable ramp beneath them. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adaptive Stairs|Collision")
	bool bGenerateRampCollision = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adaptive Stairs|Collision", meta = (EditCondition = "bGenerateRampCollision", ClampMin = "1.0", Units = "cm"))
	float RampCollisionThickness = 20.f;

	/** Simple sloped side walls prevent players and physics props from slipping through decorative rails. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adaptive Stairs|Collision")
	bool bGenerateSideCollision = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adaptive Stairs|Collision", meta = (EditCondition = "bGenerateSideCollision", ClampMin = "1.0", Units = "cm"))
	float SideCollisionHeight = 120.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adaptive Stairs|Collision", meta = (EditCondition = "bGenerateSideCollision", ClampMin = "1.0", Units = "cm"))
	float SideCollisionThickness = 20.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Adaptive Stairs|Generated")
	int32 GeneratedStepCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Adaptive Stairs|Generated", meta = (Units = "cm"))
	float GeneratedTreadDepth = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Adaptive Stairs|Generated", meta = (Units = "cm"))
	float GeneratedRiserHeight = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Adaptive Stairs|Generated", meta = (Units = "deg"))
	float GeneratedSlopeAngle = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Adaptive Stairs|Generated")
	FVector ResolvedBottomPointLocal = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Adaptive Stairs|Generated")
	bool bGeneratedLayoutWithinLimits = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Adaptive Stairs|Generated")
	FString GeneratedValidationMessage;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Adaptive Stairs|Generated")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> StepInstances;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Adaptive Stairs|Generated")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> LeftRailInstances;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Adaptive Stairs|Generated")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> RightRailInstances;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Adaptive Stairs|Generated")
	TObjectPtr<UStaticMeshComponent> TopCapComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Adaptive Stairs|Generated")
	TObjectPtr<UStaticMeshComponent> BottomCapComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Adaptive Stairs|Collision")
	TObjectPtr<UBoxComponent> RampCollision;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Adaptive Stairs|Collision")
	TObjectPtr<UBoxComponent> LeftSideCollision;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Adaptive Stairs|Collision")
	TObjectPtr<UBoxComponent> RightSideCollision;

protected:
	UPROPERTY(VisibleAnywhere, Category = "Adaptive Stairs")
	TObjectPtr<USceneComponent> SceneRoot;

#if WITH_EDITORONLY_DATA
	UPROPERTY(VisibleAnywhere, Category = "Adaptive Stairs|Preview")
	TObjectPtr<UBoxComponent> StairPreview;

	UPROPERTY(VisibleAnywhere, Category = "Adaptive Stairs|Preview")
	TObjectPtr<UArrowComponent> DirectionArrow;
#endif

private:
	void ClearGeneratedGeometry();
	void ConfigureRepeatedInstances(int32 StepCount, float TreadDepth, float RiserHeight, float HeadingYawDegrees);
	void ConfigureCap(UStaticMeshComponent* Component, UStaticMesh* Mesh, const FVector& ConnectionPoint, float HeadingYawDegrees, const FVector& ExtraOffset);
	void ConfigureCollision(const FVector& LowerConnection, float HeadingYawDegrees, float SlopeAngleDegrees);
	bool bIsRegenerating = false;
};
