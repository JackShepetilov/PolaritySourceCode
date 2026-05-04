// FoliageConversionLibrary.cpp

#include "FoliageConversionLibrary.h"

#include "EMFConvertibleFoliageType.h"
#include "EMFPhysicsProp.h"

#include "InstancedFoliageActor.h"
#include "FoliageInstancedStaticMeshComponent.h"
#include "FoliageType.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	/** Find the UFoliageType that owns the given component using only public API.
	 *  AInstancedFoliageActor::FoliageInfos is private in UE 5.6, so we iterate the
	 *  publicly-exposed list of used foliage types and look up each FFoliageInfo.
	 *  Cost is O(N) over foliage types on the level (typically <50), negligible per shot. */
	UFoliageType* FindFoliageTypeForComponent(AInstancedFoliageActor& IFA, UFoliageInstancedStaticMeshComponent& Component)
	{
		const TArray<UFoliageType*> UsedTypes = IFA.GetUsedFoliageTypes();
		for (UFoliageType* FT : UsedTypes)
		{
			if (!FT)
			{
				continue;
			}
			if (FFoliageInfo* Info = IFA.FindMesh(FT))
			{
				if (Info->GetComponent() == &Component)
				{
					return FT;
				}
			}
		}
		return nullptr;
	}
}

AEMFPhysicsProp* UFoliageConversionLibrary::TryConvertFoliageInstance(const FHitResult& Hit, float DamageDealt)
{
	// --- 1. Validate the hit was on a foliage instance ---
	AInstancedFoliageActor* IFA = Cast<AInstancedFoliageActor>(Hit.GetActor());
	if (!IFA)
	{
		return nullptr;
	}

	UFoliageInstancedStaticMeshComponent* FISMC = Cast<UFoliageInstancedStaticMeshComponent>(Hit.GetComponent());
	if (!FISMC)
	{
		return nullptr;
	}

	const int32 InstanceIndex = Hit.Item;
	if (InstanceIndex < 0 || InstanceIndex >= FISMC->GetInstanceCount())
	{
		return nullptr;
	}

	// --- 2. Resolve FoliageType and check it's convertible ---
	UFoliageType* FoliageType = FindFoliageTypeForComponent(*IFA, *FISMC);
	UEMFConvertibleFoliageType* ConvType = Cast<UEMFConvertibleFoliageType>(FoliageType);
	if (!ConvType || !ConvType->PropClass)
	{
		return nullptr;
	}

	// Damage gate (0 disables gating)
	if (ConvType->MinDamageToConvert > 0.0f && DamageDealt < ConvType->MinDamageToConvert)
	{
		return nullptr;
	}

	// --- 3. Cache instance transform + mesh BEFORE removing the instance.
	//        RemoveInstance uses RemoveAtSwap and invalidates indices. ---
	FTransform InstanceTransform;
	if (!FISMC->GetInstanceTransform(InstanceIndex, InstanceTransform, /*bWorldSpace=*/true))
	{
		return nullptr;
	}

	UStaticMesh* InstanceMesh = FISMC->GetStaticMesh();

	UWorld* World = IFA->GetWorld();
	if (!World)
	{
		return nullptr;
	}

	// --- 4. Deferred spawn so we can set the mesh BEFORE BeginPlay runs.
	//        SpawnActor + SetStaticMesh after BeginPlay would let the physics body
	//        initialize against the BP's default mesh, then need a recreate. ---
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.Owner = nullptr;
	SpawnParams.Instigator = nullptr;

	AEMFPhysicsProp* SpawnedProp = World->SpawnActorDeferred<AEMFPhysicsProp>(
		ConvType->PropClass,
		InstanceTransform,
		/*Owner=*/nullptr,
		/*Instigator=*/nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

	if (!SpawnedProp)
	{
		UE_LOG(LogTemp, Warning, TEXT("[FOLIAGE_CONVERT] SpawnActorDeferred failed for class %s"),
			*GetNameSafe(ConvType->PropClass));
		return nullptr;
	}

	// Override the prop's mesh to match the foliage instance.
	// One BP_EMFProp can back many visually-different convertible foliage assets.
	if (SpawnedProp->PropMesh && InstanceMesh)
	{
		SpawnedProp->PropMesh->SetStaticMesh(InstanceMesh);
	}

	UGameplayStatics::FinishSpawningActor(SpawnedProp, InstanceTransform);

	// --- 5. Remove the foliage instance LAST.
	//        Note: HISM RemoveInstance uses RemoveAtSwap and shuffles the last
	//        instance into this slot, so any cached Hit.Item for this component
	//        from earlier in the same frame is now invalid. ---
	FISMC->RemoveInstance(InstanceIndex);

	UE_LOG(LogTemp, Log, TEXT("[FOLIAGE_CONVERT] Converted instance %d of %s -> %s at %s"),
		InstanceIndex,
		*GetNameSafe(InstanceMesh),
		*GetNameSafe(SpawnedProp),
		*InstanceTransform.GetLocation().ToString());

	return SpawnedProp;
}
