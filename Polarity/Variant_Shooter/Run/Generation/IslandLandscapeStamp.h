// Editor-authored procedural island stamp backed by Unreal's Landscape Patch system.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IslandLandscapeStamp.generated.h"

class ALandscape;
class UArrowComponent;
class UBoxComponent;
class USceneComponent;
class ULandscapeTexturePatch;
class UTexture2D;

UCLASS(BlueprintType)
class POLARITY_API AIslandLandscapeStamp : public AActor
{
	GENERATED_BODY()

public:
	AIslandLandscapeStamp();
	virtual void OnConstruction(const FTransform& Transform) override;

	/** Rebuild the procedural height and paint textures immediately. */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Island Stamp")
	void RegenerateStamp();

	/** Bind to TargetLandscape, or to the nearest Landscape when it is not assigned. */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Island Stamp")
	void AssignToLandscape();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island Stamp|Target")
	TObjectPtr<ALandscape> TargetLandscape;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island Stamp|Target")
	FName PatchEditLayerName = TEXT("Patches");

	/** Full rectangular foundation stamp used once per level to replace the old generated terrain with a flat seabed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island Stamp|Target")
	bool bSeabedBaseStamp = false;

	/** Patch ordering inside the shared Patches edit layer. Base seabed uses 1000; islands use 1100 and above. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island Stamp|Target")
	double PatchPriority = 1100.0;

	/** Average shoreline diameter. Actor scale is intentionally ignored so the slope angle stays exact. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island Stamp|Shape", meta = (ClampMin = "1000.0", UIMin = "2000.0", UIMax = "30000.0"))
	float SizeX = 12000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island Stamp|Shape", meta = (ClampMin = "1000.0", UIMin = "2000.0", UIMax = "30000.0"))
	float SizeY = 12000.f;

	/** Vertical distance from the plateau to the shoreline. Actor Z is the plateau height. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island Stamp|Shape", meta = (ClampMin = "100.0", UIMin = "300.0", UIMax = "2000.0"))
	float PlateauToShoreHeight = 800.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island Stamp|Beach", meta = (ClampMin = "5.0", ClampMax = "60.0", UIMin = "10.0", UIMax = "45.0", Units = "Degrees"))
	float BeachSlopeAngle = 30.f;

	/** Soft transition from the constant beach angle into the flat plateau. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island Stamp|Beach", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "500.0"))
	float PlateauBlendWidth = 200.f;

	/** Horizontal distance over which the underwater slope blends into the sea floor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island Stamp|Underwater", meta = (ClampMin = "500.0", UIMin = "1000.0", UIMax = "10000.0"))
	float UnderwaterBlendLength = 4500.f;

	/** Sea-floor depth below the plateau at the outer edge of this patch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island Stamp|Underwater", meta = (ClampMin = "500.0", UIMin = "1000.0", UIMax = "5000.0"))
	float SeabedDepthBelowPlateau = 2300.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island Stamp|Noise", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "1500.0"))
	float NoiseStrength = 250.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island Stamp|Noise", meta = (ClampMin = "100.0", UIMin = "500.0", UIMax = "10000.0"))
	float NoiseScale = 3000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island Stamp|Noise", meta = (ClampMin = "1", ClampMax = "6"))
	int32 NoiseOctaves = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island Stamp|Noise")
	int32 NoiseSeed = 1337;

	/** Distance inland from the shoreline before grass starts replacing sand. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island Stamp|Paint", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "3000.0"))
	float GrassInset = 700.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island Stamp|Paint", meta = (ClampMin = "1.0", UIMin = "50.0", UIMax = "1000.0"))
	float GrassBlendWidth = 300.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island Stamp|Paint")
	FName SandLayerName = TEXT("Sand_01");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island Stamp|Paint")
	FName GrassLayerName = TEXT("Grass");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island Stamp|Paint")
	FName BaseLayerName = TEXT("Base");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island Stamp|Quality", meta = (ClampMin = "65", ClampMax = "1025"))
	int32 TextureResolution = 257;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island Stamp|Arena")
	FName ArenaSlotId = TEXT("Slot_1");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island Stamp|Arena")
	FVector ArenaAnchorOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island Stamp|Arena", meta = (Units = "Degrees"))
	float ArenaAnchorYaw = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Island Stamp")
	TObjectPtr<ULandscapeTexturePatch> LandscapePatch;

protected:
	UPROPERTY(VisibleAnywhere, Category = "Island Stamp")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category = "Island Stamp|Preview")
	TObjectPtr<UBoxComponent> PlateauPreview;

	UPROPERTY(VisibleAnywhere, Category = "Island Stamp|Preview")
	TObjectPtr<UArrowComponent> ArenaAnchorPreview;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> HeightTexture;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> SandTexture;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> GrassTexture;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> BaseTexture;

private:
	void RebuildTextures();
	void ConfigurePatch();
	void UpdatePreview();
	UTexture2D* CreateFloatTexture(const FString& DebugName, const TArray<FFloat16Color>& Pixels, int32 Resolution) const;
	float SampleNoise(const FVector2D& LocalPosition) const;
	bool bIsRebuilding = false;
};
