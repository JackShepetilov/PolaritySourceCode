// FoliageConversionLibrary.h
// Static helpers that promote a foliage instance into a runtime EMFPhysicsProp.
//
// Called from weapon hit paths (hitscan, laser beam, melee) so that an ionizing
// strike on a foliage instance painted from a UEMFConvertibleFoliageType swaps
// the visual instance for a real physics-simulated AEMFPhysicsProp at the same
// world transform, with PropMesh set to the foliage instance's StaticMesh.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "FoliageConversionLibrary.generated.h"

class AEMFPhysicsProp;
struct FHitResult;

UCLASS()
class POLARITY_API UFoliageConversionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * If the supplied hit struck a foliage instance painted from a
	 * UEMFConvertibleFoliageType (with a valid PropClass), spawn the configured
	 * EMFPhysicsProp at the instance's world transform, override its PropMesh
	 * with the foliage instance's StaticMesh, and remove the instance.
	 *
	 * @param Hit          Hit result from a line trace / sweep.
	 * @param DamageDealt  Damage that triggered the hit. Used against the
	 *                     FoliageType's MinDamageToConvert threshold. Pass 0
	 *                     for laser tick to bypass damage gating.
	 * @return The freshly spawned prop, or nullptr if the hit did not match a
	 *         convertible foliage instance.
	 */
	UFUNCTION(BlueprintCallable, Category = "EMF|Foliage Conversion")
	static AEMFPhysicsProp* TryConvertFoliageInstance(const FHitResult& Hit, float DamageDealt = 0.0f);
};
