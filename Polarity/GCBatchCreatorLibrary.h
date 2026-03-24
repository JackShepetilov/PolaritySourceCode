// GCBatchCreatorLibrary.h
// Editor-only Blueprint Function Library for batch Geometry Collection creation
// Used by EUW_BatchGCCreator widget

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GCBatchCreatorLibrary.generated.h"

class UStaticMesh;
class UGeometryCollection;

/**
 * Result of a single GC creation attempt.
 */
USTRUCT(BlueprintType)
struct FGCCreationResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly)
	FString Message;

	UPROPERTY(BlueprintReadOnly)
	FString CreatedAssetPath;

	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<UGeometryCollection> CreatedGC = nullptr;
};

/**
 * Editor-only library for batch-creating Voronoi-fractured Geometry Collection assets
 * from Static Meshes. Naming convention: GC_{MeshName} saved next to the source mesh.
 *
 * Fracture approach: triangle-centroid Voronoi assignment. Each piece gets its own
 * vertex set (shared boundary vertices are duplicated). Good enough for destruction
 * props — pieces fly apart quickly and cut boundaries are invisible.
 *
 * All functions are editor-only (no-op / empty return in non-editor builds).
 */
UCLASS()
class UGCBatchCreatorLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Create a Voronoi-fractured GC asset from a Static Mesh.
	 * Saves as GC_{MeshName} in the same folder as the source mesh.
	 *
	 * @param SourceMesh         The static mesh to fracture
	 * @param PieceCount         Number of Voronoi pieces (0 = auto based on mesh size)
	 * @param bOverwriteExisting If true, overwrites existing GC with the same name
	 */
	UFUNCTION(BlueprintCallable, Category = "GC Batch Creator", meta = (Keywords = "geometry collection create batch voronoi"))
	static FGCCreationResult CreateGCFromStaticMesh(UStaticMesh* SourceMesh, int32 PieceCount = 0, bool bOverwriteExisting = false);

	/**
	 * Batch-create fractured GCs from multiple Static Meshes.
	 * Skips meshes that already have a matching GC (unless bOverwriteExisting).
	 */
	UFUNCTION(BlueprintCallable, Category = "GC Batch Creator")
	static TArray<FGCCreationResult> BatchCreateGCFromStaticMeshes(const TArray<UStaticMesh*>& SourceMeshes, int32 PieceCount = 0, bool bOverwriteExisting = false);

	/** Calculate automatic piece count based on mesh bounding box dimensions. */
	UFUNCTION(BlueprintPure, Category = "GC Batch Creator")
	static int32 CalculateAutoPieceCount(UStaticMesh* SourceMesh);

	/** Check if GC_{MeshName} already exists in the same folder. */
	UFUNCTION(BlueprintPure, Category = "GC Batch Creator")
	static bool DoesGCExistForMesh(UStaticMesh* SourceMesh);

	/** Returns "GC_{MeshName}". */
	UFUNCTION(BlueprintPure, Category = "GC Batch Creator")
	static FString GetExpectedGCName(UStaticMesh* SourceMesh);

	/** Get all Static Meshes currently selected in the Content Browser. */
	UFUNCTION(BlueprintCallable, Category = "GC Batch Creator")
	static TArray<UStaticMesh*> GetSelectedStaticMeshes();
};
