// EMFFoliageSettings.h
// Project-level configuration for foliage→EMFPhysicsProp conversion.
//
// Lives in Project Settings -> Polarity -> EMF Foliage Conversion.
// Add one FEMFFoliageEntry per FoliageType you want to make ionizable.
//
// We can't subclass UFoliageType_InstancedStaticMesh in a project module
// (it's MinimalAPI — virtual symbols aren't exported from Foliage.dll, so
// the linker fails on a derived class). Instead we keep the engine
// FoliageType untouched and store the mapping
// (FoliageType asset -> AEMFPhysicsProp subclass) here.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "EMFFoliageSettings.generated.h"

class UFoliageType;
class AEMFPhysicsProp;

/** One row of the convertible-foliage table. */
USTRUCT(BlueprintType)
struct POLARITY_API FEMFFoliageEntry
{
	GENERATED_BODY()

	/** Foliage type whose painted instances should convert on ionizing hit. */
	UPROPERTY(EditAnywhere, Category = "EMF")
	TSoftObjectPtr<UFoliageType> FoliageType;

	/** Physics prop class spawned in place of the instance (typically BP_EMFProp).
	 *  The spawned prop's PropMesh is overridden with the instance's StaticMesh,
	 *  so one PropClass can back many visually-different convertible foliage types. */
	UPROPERTY(EditAnywhere, Category = "EMF")
	TSoftClassPtr<AEMFPhysicsProp> PropClass;

	/** Only hits dealing >= this much damage will trigger conversion.
	 *  0 = any hit converts. Useful to gate conversion behind specific weapons. */
	UPROPERTY(EditAnywhere, Category = "EMF", meta = (ClampMin = "0.0"))
	float MinDamageToConvert = 0.0f;
};

/**
 * Project Settings entry: list of foliage types that can be promoted to a
 * physics prop on ionizing weapon / melee hit.
 */
UCLASS(Config = Game, defaultconfig, meta = (DisplayName = "EMF Foliage Conversion"))
class POLARITY_API UEMFFoliageSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UEMFFoliageSettings();

	/** Foliage types that should be treated as convertible. Order is irrelevant —
	 *  the first matching entry for a given FoliageType wins. */
	UPROPERTY(EditAnywhere, Config, Category = "EMF Foliage Conversion")
	TArray<FEMFFoliageEntry> Entries;

	/** Resolve the entry (if any) for a given foliage type. Returns nullptr if
	 *  the type is not registered for conversion. The returned pointer is stable
	 *  for the lifetime of the settings object. */
	const FEMFFoliageEntry* FindEntryForFoliageType(const UFoliageType* InType) const;
};
