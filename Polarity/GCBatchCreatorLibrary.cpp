// GCBatchCreatorLibrary.cpp
// Batch Voronoi-fractured Geometry Collection creation from Static Meshes

#if WITH_EDITOR

#include "GCBatchCreatorLibrary.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "GeometryCollection/GeometryCollectionEngineConversion.h"
#include "GeometryCollection/GeometryCollectionFactory.h"
#include "FractureEngineFracturing.h"
#include "Serialization/BufferArchive.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "UObject/SavePackage.h"
#include "UObject/Linker.h"
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"

// ================================================================
// Internal types
// ================================================================

namespace
{
	struct FBatchVertex
	{
		FVector3f Position;
		FVector3f Normal;
		FVector3f TangentX;
		float TangentSign;
		FVector2f UV0;
	};

	struct FBatchFace
	{
		int32 V0, V1, V2;
		int32 MaterialIdx;
	};

	struct FPieceGeometry
	{
		TArray<FBatchVertex> Vertices;
		TArray<FBatchFace> Faces;
	};
}

// ================================================================
// Extract mesh data from StaticMesh LOD0
// ================================================================
static bool ExtractMeshData(const UStaticMesh* Mesh, TArray<FBatchVertex>& OutVerts, TArray<FBatchFace>& OutFaces)
{
	if (!Mesh || !Mesh->GetRenderData() || Mesh->GetRenderData()->LODResources.Num() == 0)
	{
		return false;
	}

	const FStaticMeshLODResources& LOD = Mesh->GetRenderData()->LODResources[0];
	const FPositionVertexBuffer& PosBuffer = LOD.VertexBuffers.PositionVertexBuffer;
	const FStaticMeshVertexBuffer& VertBuffer = LOD.VertexBuffers.StaticMeshVertexBuffer;

	const int32 NumVerts = PosBuffer.GetNumVertices();
	OutVerts.SetNum(NumVerts);

	for (int32 i = 0; i < NumVerts; i++)
	{
		OutVerts[i].Position = PosBuffer.VertexPosition(i);
		const FVector4f TZ = VertBuffer.VertexTangentZ(i);
		OutVerts[i].Normal = FVector3f(TZ.X, TZ.Y, TZ.Z);
		OutVerts[i].TangentSign = TZ.W;
		const FVector4f TX = VertBuffer.VertexTangentX(i);
		OutVerts[i].TangentX = FVector3f(TX.X, TX.Y, TX.Z);
		OutVerts[i].UV0 = (VertBuffer.GetNumTexCoords() > 0)
			? VertBuffer.GetVertexUV(i, 0)
			: FVector2f::ZeroVector;
	}

	TArray<uint32> RawIndices;
	LOD.IndexBuffer.GetCopy(RawIndices);

	for (const FStaticMeshSection& Section : LOD.Sections)
	{
		for (uint32 t = 0; t < Section.NumTriangles; t++)
		{
			const int32 Base = Section.FirstIndex + t * 3;
			FBatchFace F;
			F.V0 = RawIndices[Base];
			F.V1 = RawIndices[Base + 1];
			F.V2 = RawIndices[Base + 2];
			F.MaterialIdx = Section.MaterialIndex;
			OutFaces.Add(F);
		}
	}

	return OutVerts.Num() > 0 && OutFaces.Num() > 0;
}

// ================================================================
// Voronoi partition: assign faces to nearest site by centroid,
// then build per-piece vertex/face arrays with vertex duplication
// ================================================================
static TArray<FPieceGeometry> PartitionMesh(
	const TArray<FBatchVertex>& Verts,
	const TArray<FBatchFace>& Faces,
	int32 PieceCount,
	uint32 Seed)
{
	// Bounding box
	FBox Bounds(ForceInit);
	for (const auto& V : Verts)
	{
		Bounds += FVector(V.Position);
	}

	// Generate Voronoi sites (slightly inset to avoid thin edge slices)
	FRandomStream Rand(Seed);
	TArray<FVector> Sites;
	Sites.SetNum(PieceCount);
	const FVector Center = Bounds.GetCenter();
	const FVector Extent = Bounds.GetExtent() * 0.85f;

	for (int32 i = 0; i < PieceCount; i++)
	{
		Sites[i] = Center + FVector(
			Rand.FRandRange(-Extent.X, Extent.X),
			Rand.FRandRange(-Extent.Y, Extent.Y),
			Rand.FRandRange(-Extent.Z, Extent.Z));
	}

	// Assign faces to nearest site by centroid
	TArray<TArray<int32>> PieceFaces;
	PieceFaces.SetNum(PieceCount);

	for (int32 f = 0; f < Faces.Num(); f++)
	{
		const FVector Centroid = (
			FVector(Verts[Faces[f].V0].Position) +
			FVector(Verts[Faces[f].V1].Position) +
			FVector(Verts[Faces[f].V2].Position)) / 3.0;

		float MinDist = MAX_FLT;
		int32 BestSite = 0;
		for (int32 s = 0; s < Sites.Num(); s++)
		{
			const float D = FVector::DistSquared(Centroid, Sites[s]);
			if (D < MinDist)
			{
				MinDist = D;
				BestSite = s;
			}
		}
		PieceFaces[BestSite].Add(f);
	}

	// Build per-piece geometry (duplicate shared vertices for clean separation)
	TArray<FPieceGeometry> Result;
	for (int32 p = 0; p < PieceCount; p++)
	{
		if (PieceFaces[p].Num() == 0)
		{
			continue;
		}

		FPieceGeometry Piece;
		TMap<int32, int32> VertRemap; // original index -> piece-local index

		for (int32 fi : PieceFaces[p])
		{
			const FBatchFace& Src = Faces[fi];
			const int32 OrigIdx[3] = { Src.V0, Src.V1, Src.V2 };
			int32 LocalIdx[3];

			for (int32 c = 0; c < 3; c++)
			{
				if (const int32* Found = VertRemap.Find(OrigIdx[c]))
				{
					LocalIdx[c] = *Found;
				}
				else
				{
					LocalIdx[c] = Piece.Vertices.Num();
					VertRemap.Add(OrigIdx[c], LocalIdx[c]);
					Piece.Vertices.Add(Verts[OrigIdx[c]]);
				}
			}

			FBatchFace NewFace;
			NewFace.V0 = LocalIdx[0];
			NewFace.V1 = LocalIdx[1];
			NewFace.V2 = LocalIdx[2];
			NewFace.MaterialIdx = Src.MaterialIdx;
			Piece.Faces.Add(NewFace);
		}

		Result.Add(MoveTemp(Piece));
	}

	return Result;
}

// ================================================================
// Build a FMeshDescription from a single piece's vertex/face data
// ================================================================
static FMeshDescription BuildPieceMeshDescription(const FPieceGeometry& Piece)
{
	FMeshDescription MeshDesc;
	FStaticMeshAttributes Attribs(MeshDesc);
	Attribs.Register();

	// Collect unique material indices used by this piece
	TSet<int32> UsedMaterials;
	for (const FBatchFace& F : Piece.Faces)
	{
		UsedMaterials.Add(F.MaterialIdx);
	}

	// Create polygon groups (one per material)
	TMap<int32, FPolygonGroupID> MatToGroup;
	for (int32 MatIdx : UsedMaterials)
	{
		FPolygonGroupID GroupID = MeshDesc.CreatePolygonGroup();
		MatToGroup.Add(MatIdx, GroupID);
	}

	// Reserve
	MeshDesc.ReserveNewVertices(Piece.Vertices.Num());
	MeshDesc.ReserveNewVertexInstances(Piece.Vertices.Num());
	MeshDesc.ReserveNewPolygons(Piece.Faces.Num());
	MeshDesc.ReserveNewEdges(Piece.Faces.Num() * 3);

	// Create vertices + vertex instances (1:1)
	TArray<FVertexInstanceID> InstanceIDs;
	InstanceIDs.SetNum(Piece.Vertices.Num());

	TArrayView<FVector3f> Positions = Attribs.GetVertexPositions().GetRawArray();

	for (int32 v = 0; v < Piece.Vertices.Num(); v++)
	{
		const FBatchVertex& Src = Piece.Vertices[v];

		FVertexID VID = MeshDesc.CreateVertex();
		Attribs.GetVertexPositions()[VID] = Src.Position;

		FVertexInstanceID IID = MeshDesc.CreateVertexInstance(VID);
		Attribs.GetVertexInstanceNormals()[IID] = Src.Normal;
		Attribs.GetVertexInstanceTangents()[IID] = Src.TangentX;
		Attribs.GetVertexInstanceBinormalSigns()[IID] = Src.TangentSign;
		Attribs.GetVertexInstanceUVs().Set(IID, 0, Src.UV0);

		InstanceIDs[v] = IID;
	}

	// Create polygons (triangles)
	for (const FBatchFace& Face : Piece.Faces)
	{
		TArray<FVertexInstanceID> TriInstances;
		TriInstances.Add(InstanceIDs[Face.V0]);
		TriInstances.Add(InstanceIDs[Face.V1]);
		TriInstances.Add(InstanceIDs[Face.V2]);

		MeshDesc.CreatePolygon(MatToGroup[Face.MaterialIdx], TriInstances);
	}

	return MeshDesc;
}

// ================================================================
// Build the GC from pieces using the proper engine API
// ================================================================
static bool BuildFracturedGC(
	UGeometryCollection* GCAsset,
	const UStaticMesh* SourceMesh,
	const TArray<FPieceGeometry>& Pieces)
{
	if (!GCAsset || Pieces.Num() == 0)
	{
		return false;
	}

	// Step 1: Append full mesh as single piece (the only serialization-safe path in UE5.6)
	TArray<UMaterialInterface*> MeshMaterials;
	for (const FStaticMaterial& Mat : SourceMesh->GetStaticMaterials())
	{
		MeshMaterials.Add(Mat.MaterialInterface);
	}

	FGeometryCollectionEngineConversion::AppendStaticMesh(
		SourceMesh, MeshMaterials, FTransform::Identity, GCAsset,
		true, false, false, false);

	// Step 2: Voronoi fracture using the engine's own fracture algorithm
	FGeometryCollection* GC = GCAsset->GetGeometryCollection().Get();
	if (!GC)
	{
		return false;
	}

	const int32 PieceCount = Pieces.Num();

	// Generate Voronoi sites inside mesh bounding box
	const FBox MeshBounds = SourceMesh->GetBoundingBox();
	const FVector Center = MeshBounds.GetCenter();
	const FVector Extent = MeshBounds.GetExtent() * 0.85f; // Inset to avoid thin edge pieces

	const uint32 Seed = GetTypeHash(SourceMesh->GetName());
	FRandomStream Rand(Seed);

	TArray<FVector> VoronoiSites;
	VoronoiSites.Reserve(PieceCount);
	for (int32 i = 0; i < PieceCount; i++)
	{
		VoronoiSites.Add(Center + FVector(
			Rand.FRandRange(-Extent.X, Extent.X),
			Rand.FRandRange(-Extent.Y, Extent.Y),
			Rand.FRandRange(-Extent.Z, Extent.Z)));
	}

	// Select all existing transforms for fracturing
	FDataflowTransformSelection TransformSelection;
	TransformSelection.Initialize(GC->NumElements(FTransformCollection::TransformGroup), true);

	// Fracture in-place using engine API
	FFractureEngineFracturing::VoronoiFracture(
		*GC,
		TransformSelection,
		VoronoiSites,
		FTransform::Identity,
		Rand.GetCurrentSeed(),
		1.0f,   // ChanceToFracture (100%)
		true,   // SplitIslands
		0.0f,   // Grout
		0.0f,   // Amplitude (no noise)
		0.1f,   // Frequency
		0.5f,   // Persistence
		2.0f,   // Lacunarity
		4,      // OctaveNumber
		1.0f,   // PointSpacing
		true,   // AddSamplesForCollision
		5.0f    // CollisionSampleSpacing
	);

	UE_LOG(LogTemp, Log, TEXT("GCBatch: Voronoi fracture → %d pieces from %d sites"),
		GC->NumElements(FTransformCollection::TransformGroup), PieceCount);

	return true;
}

// ================================================================
// Public API
// ================================================================

int32 UGCBatchCreatorLibrary::CalculateAutoPieceCount(UStaticMesh* SourceMesh)
{
	if (!SourceMesh) return 5;

	const FBox Bounds = SourceMesh->GetBoundingBox();
	const float MaxDim = Bounds.GetSize().GetMax();

	if (MaxDim < 30.0f) return 3;
	if (MaxDim < 60.0f) return 5;
	if (MaxDim < 120.0f) return 7;
	if (MaxDim < 250.0f) return 10;
	if (MaxDim < 500.0f) return 13;
	return 16;
}

FGCCreationResult UGCBatchCreatorLibrary::CreateGCFromStaticMesh(
	UStaticMesh* SourceMesh,
	int32 PieceCount,
	bool bOverwriteExisting)
{
	FGCCreationResult Result;

	if (!SourceMesh)
	{
		Result.Message = TEXT("Null source mesh");
		return Result;
	}

	// Determine output path
	const FString MeshPath = FPackageName::GetLongPackagePath(SourceMesh->GetOutermost()->GetName());
	const FString GCName = FString::Printf(TEXT("GC_%s"), *SourceMesh->GetName());
	const FString FullPath = MeshPath / GCName;

	// Check existing
	if (!bOverwriteExisting && FPackageName::DoesPackageExist(FullPath))
	{
		Result.Message = FString::Printf(TEXT("Already exists: %s (skipped)"), *GCName);
		return Result;
	}

	// Extract mesh geometry
	TArray<FBatchVertex> Verts;
	TArray<FBatchFace> Faces;
	if (!ExtractMeshData(SourceMesh, Verts, Faces))
	{
		Result.Message = FString::Printf(TEXT("Failed to extract mesh data from %s"), *SourceMesh->GetName());
		return Result;
	}

	// Auto piece count
	if (PieceCount <= 0)
	{
		PieceCount = CalculateAutoPieceCount(SourceMesh);
	}

	// Voronoi partition
	const uint32 Seed = GetTypeHash(SourceMesh->GetName());
	TArray<FPieceGeometry> Pieces = PartitionMesh(Verts, Faces, PieceCount, Seed);

	if (Pieces.Num() < 2)
	{
		Result.Message = FString::Printf(TEXT("Mesh %s too simple to fracture (< 2 pieces from %d faces)"),
			*SourceMesh->GetName(), Faces.Num());
		return Result;
	}

	// Create asset via GeometryCollection factory — handles Dataflow schema
	// and internal initialization that NewObject skips.
	UPackage* Package = CreatePackage(*FullPath);
	Package->FullyLoad();

	UGeometryCollectionFactory* Factory = NewObject<UGeometryCollectionFactory>();
	UGeometryCollection* GCAsset = Cast<UGeometryCollection>(
		Factory->FactoryCreateNew(UGeometryCollection::StaticClass(),
			Package, *GCName, RF_Public | RF_Standalone, nullptr, GWarn));

	// Build GC (single piece via AppendStaticMesh — the only path that serializes in UE5.6)
	if (!BuildFracturedGC(GCAsset, SourceMesh, Pieces))
	{
		Result.Message = FString::Printf(TEXT("Failed to build GC for %s"), *SourceMesh->GetName());
		return Result;
	}

	// Save to disk
	FAssetRegistryModule::AssetCreated(GCAsset);
	GCAsset->MarkPackageDirty();

	const FString PackageFilename = FPackageName::LongPackageNameToFilename(
		FullPath, FPackageName::GetAssetPackageExtension());

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	const bool bSaved = UPackage::SavePackage(Package, GCAsset, *PackageFilename, SaveArgs);

	// Force reload from disk so PostLoad runs (builds render proxies).
	UGeometryCollection* FinalGC = GCAsset;
	if (bSaved)
	{
		const FString AssetPath = FullPath + TEXT(".") + GCName;

		// Rename old object out of the way so LoadObject is forced to load from disk
		GCAsset->Rename(*FString::Printf(TEXT("%s_OLD"), *GCName), GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
		ResetLoaders(Package);
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		FinalGC = LoadObject<UGeometryCollection>(nullptr, *AssetPath);

		UE_LOG(LogTemp, Warning, TEXT("GCBatch RELOAD: %s new=%p old=%p same=%d"),
			*GCName, (void*)FinalGC, (void*)GCAsset, FinalGC == GCAsset);
	}

	Result.bSuccess = bSaved && FinalGC != nullptr;
	Result.CreatedAssetPath = FullPath + TEXT(".") + GCName;
	Result.CreatedGC = FinalGC;
	Result.Message = bSaved
		? FString::Printf(TEXT("Created %s (%d pieces)"), *GCName, Pieces.Num())
		: FString::Printf(TEXT("Failed to save %s"), *GCName);

	if (bSaved)
	{
		UE_LOG(LogTemp, Log, TEXT("GCBatch: %s"), *Result.Message);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("GCBatch: %s"), *Result.Message);
	}

	return Result;
}

TArray<FGCCreationResult> UGCBatchCreatorLibrary::BatchCreateGCFromStaticMeshes(
	const TArray<UStaticMesh*>& SourceMeshes,
	int32 PieceCount,
	bool bOverwriteExisting)
{
	TArray<FGCCreationResult> Results;
	Results.Reserve(SourceMeshes.Num());

	for (UStaticMesh* Mesh : SourceMeshes)
	{
		Results.Add(CreateGCFromStaticMesh(Mesh, PieceCount, bOverwriteExisting));
	}

	return Results;
}

bool UGCBatchCreatorLibrary::DoesGCExistForMesh(UStaticMesh* SourceMesh)
{
	if (!SourceMesh) return false;

	const FString MeshPath = FPackageName::GetLongPackagePath(SourceMesh->GetOutermost()->GetName());
	const FString GCName = FString::Printf(TEXT("GC_%s"), *SourceMesh->GetName());
	const FString FullPath = MeshPath / GCName;

	return FPackageName::DoesPackageExist(FullPath);
}

FString UGCBatchCreatorLibrary::GetExpectedGCName(UStaticMesh* SourceMesh)
{
	if (!SourceMesh) return TEXT("");
	return FString::Printf(TEXT("GC_%s"), *SourceMesh->GetName());
}

TArray<UStaticMesh*> UGCBatchCreatorLibrary::GetSelectedStaticMeshes()
{
	TArray<UStaticMesh*> Result;

	FContentBrowserModule& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	TArray<FAssetData> SelectedAssets;
	ContentBrowser.Get().GetSelectedAssets(SelectedAssets);

	for (const FAssetData& Asset : SelectedAssets)
	{
		if (Asset.GetClass() == UStaticMesh::StaticClass())
		{
			if (UStaticMesh* Mesh = Cast<UStaticMesh>(Asset.GetAsset()))
			{
				Result.Add(Mesh);
			}
		}
	}

	return Result;
}

#else // !WITH_EDITOR — stubs for non-editor builds

#include "GCBatchCreatorLibrary.h"

FGCCreationResult UGCBatchCreatorLibrary::CreateGCFromStaticMesh(UStaticMesh*, int32, bool) { return {}; }
TArray<FGCCreationResult> UGCBatchCreatorLibrary::BatchCreateGCFromStaticMeshes(const TArray<UStaticMesh*>&, int32, bool) { return {}; }
int32 UGCBatchCreatorLibrary::CalculateAutoPieceCount(UStaticMesh*) { return 5; }
bool UGCBatchCreatorLibrary::DoesGCExistForMesh(UStaticMesh*) { return false; }
FString UGCBatchCreatorLibrary::GetExpectedGCName(UStaticMesh*) { return {}; }
TArray<UStaticMesh*> UGCBatchCreatorLibrary::GetSelectedStaticMeshes() { return {}; }

#endif // WITH_EDITOR
