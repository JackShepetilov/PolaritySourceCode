// Editor-authored bridge placeholder that can later grow into a spline/adaptive mesh bridge.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IslandBridgeActor.generated.h"

class UArrowComponent;
class UBoxComponent;
class UHierarchicalInstancedStaticMeshComponent;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;
class AArenaManager;
class ABiomeArenaAnchor;

/**
 * A straight bridge whose actor origin is the start point and local +X points to the end.
 *
 * Blender contract for BridgeStaticMesh:
 * - forward/length axis: +X
 * - width axis: Y
 * - up axis: Z
 * - any pivot is accepted; mesh bounds are aligned to start at actor X=0 automatically
 */
UCLASS(BlueprintType)
class POLARITY_API AIslandBridgeActor : public AActor
{
	GENERATED_BODY()

public:
	AIslandBridgeActor();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnConstruction(const FTransform& Transform) override;

	/** Re-apply size, mesh alignment, collision, and editor preview immediately. */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Island Bridge")
	void RegenerateBridge();

	/** Distance from the actor origin to the bridge end along local +X. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island Bridge|Shape", meta = (ClampMin = "100.0", UIMin = "1000.0", UIMax = "10000.0", Units = "cm"))
	float Length = 6000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island Bridge|Shape", meta = (ClampMin = "50.0", UIMin = "200.0", UIMax = "2000.0", Units = "cm"))
	float Width = 600.f;

	/** Used by the placeholder and, optionally, to scale the imported mesh vertically. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island Bridge|Shape", meta = (ClampMin = "10.0", UIMin = "25.0", UIMax = "500.0", Units = "cm"))
	float Height = 100.f;

	/** Keep disabled for most authored meshes so changing bridge length does not distort thickness. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island Bridge|Shape")
	bool bScaleMeshHeight = false;

	/** Temporary or final straight bridge mesh. It is fitted to Length and Width. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island Bridge|Mesh")
	TObjectPtr<UStaticMesh> BridgeStaticMesh;

	/** Exact authored length of one repeatable module. The Blender delivery uses 200 cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island Bridge|Modular", meta = (ClampMin = "10.0", Units = "cm"))
	float SegmentLength = 200.f;

	/** Width used when the modules were authored in Blender. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island Bridge|Modular", meta = (ClampMin = "10.0", Units = "cm"))
	float AuthoredModuleWidth = 600.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island Bridge|Modular")
	TObjectPtr<UStaticMesh> DeckSegmentMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island Bridge|Modular")
	TObjectPtr<UStaticMesh> LeftRailSegmentMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island Bridge|Modular")
	TObjectPtr<UStaticMesh> RightRailSegmentMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island Bridge|Modular")
	TObjectPtr<UStaticMesh> StartCapMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island Bridge|Modular")
	TObjectPtr<UStaticMesh> EndCapMesh;

	/** Fine alignment for imported modules whose authored object origins are not at the bridge origin. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island Bridge|Modular|Offsets")
	FVector DeckSegmentOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island Bridge|Modular|Offsets")
	FVector LeftRailSegmentOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island Bridge|Modular|Offsets")
	FVector RightRailSegmentOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island Bridge|Modular|Offsets")
	FVector StartCapOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island Bridge|Modular|Offsets")
	FVector EndCapOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island Bridge|Collision")
	bool bGenerateCollision = true;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Island Bridge|Progression")
	TObjectPtr<ABiomeArenaAnchor> UnlockAfterAnchor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island Bridge|Progression")
	bool bStartsLocked = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island Bridge|Progression", meta = (ClampMin = "0.0", Units = "cm"))
	float BlockerLengthPadding = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island Bridge|Progression", meta = (ClampMin = "100.0", Units = "cm"))
	float BlockerHeight = 2000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island Bridge|Progression", meta = (ClampMin = "0.0", Units = "cm"))
	float BlockerWidthPadding = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island Bridge|Progression", meta = (Units = "cm"))
	float BlockerVerticalOffset = 0.f;

	void BindUnlockSource(AArenaManager* ArenaManager, int32 ArenaIndex);

	UFUNCTION(BlueprintCallable, Category = "Island Bridge|Progression")
	void SetBridgeLocked(bool bLocked);

	UFUNCTION(BlueprintPure, Category = "Island Bridge|Progression")
	bool IsBridgeLocked() const { return bBridgeLocked; }

	UFUNCTION(BlueprintImplementableEvent, Category = "Island Bridge|Progression")
	void OnBridgeLockStateChanged(bool bLocked);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Island Bridge")
	TObjectPtr<UStaticMeshComponent> GeneratedMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Island Bridge|Generated")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> DeckInstances;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Island Bridge|Generated")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> LeftRailInstances;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Island Bridge|Generated")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> RightRailInstances;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Island Bridge|Generated")
	TObjectPtr<UStaticMeshComponent> StartCapComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Island Bridge|Generated")
	TObjectPtr<UStaticMeshComponent> EndCapComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Island Bridge|Progression")
	TObjectPtr<UBoxComponent> ProgressionBlocker;

protected:
	UPROPERTY(VisibleAnywhere, Category = "Island Bridge")
	TObjectPtr<USceneComponent> SceneRoot;

#if WITH_EDITORONLY_DATA
	UPROPERTY(VisibleAnywhere, Category = "Island Bridge|Preview")
	TObjectPtr<UBoxComponent> BridgePreview;

	UPROPERTY(VisibleAnywhere, Category = "Island Bridge|Preview")
	TObjectPtr<UArrowComponent> ForwardArrow;
#endif

private:
	void ConfigureModularInstances();
	void ConfigureCap(UStaticMeshComponent* Component, UStaticMesh* Mesh, bool bAlignToEnd, const FVector& ExtraOffset);
	void ConfigureProgressionBlocker();

	UFUNCTION()
	void HandleSourceArenaCleared();

	TWeakObjectPtr<AArenaManager> BoundArenaManager;
	int32 BoundArenaIndex = INDEX_NONE;
	bool bBridgeLocked = false;

	bool bIsRegenerating = false;
};
