// EMFConvertibleFoliageType.h
// Custom FoliageType that can be converted into a physics-simulated EMFPhysicsProp
// at runtime when hit by an ionizing weapon or melee strike.
//
// USAGE (editor):
//  1) Content Browser -> Right-click -> Miscellaneous -> Data Asset
//     -> pick "EMFConvertibleFoliageType"
//  2) On the asset: set Mesh, Collision (QueryAndPhysics + Block Visibility),
//     and PropClass (typically BP_EMFProp).
//  3) Open Foliage Mode (Shift+3) and drag this asset into the foliage panel.
//  4) Paint instances with the brush.
//
// On hit: the foliage instance is removed and a prop spawned at the same
// world transform, with the foliage instance's mesh applied to PropMesh.

#pragma once

#include "CoreMinimal.h"
#include "FoliageType_InstancedStaticMesh.h"
#include "EMFConvertibleFoliageType.generated.h"

class AEMFPhysicsProp;

/**
 * FoliageType variant whose instances can be promoted to a real EMFPhysicsProp
 * actor when struck by an ionizing weapon or melee hit.
 *
 * The spawned prop reuses the foliage instance's static mesh, so a single
 * BP_EMFProp class can back many visually-different convertible foliage assets
 * (tires, barrels, crates, etc.) — only the Mesh and PropClass need to be set.
 */
UCLASS(BlueprintType, MinimalAPI, meta = (DisplayName = "EMF Convertible Foliage Type"))
class UEMFConvertibleFoliageType : public UFoliageType_InstancedStaticMesh
{
	GENERATED_BODY()

public:
	UEMFConvertibleFoliageType();

	/** Physics prop class spawned in place of a foliage instance on ionizing hit.
	 *  Typically BP_EMFProp — the spawned actor's PropMesh is overwritten with the
	 *  foliage instance's StaticMesh, so one PropClass can handle many meshes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "EMF Conversion")
	TSubclassOf<AEMFPhysicsProp> PropClass;

	/** Optional damage threshold: only hits dealing >= this amount will trigger conversion.
	 *  0 = any hit converts. Useful to gate conversion behind specific weapons. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "EMF Conversion", meta = (ClampMin = "0.0"))
	float MinDamageToConvert = 0.0f;
};
