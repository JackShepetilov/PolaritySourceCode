// FoliageConversionLibrary.cpp

#include "FoliageConversionLibrary.h"

#include "EMFFoliageSettings.h"
#include "EMFPhysicsProp.h"

#include "InstancedFoliageActor.h"
#include "FoliageInstancedStaticMeshComponent.h"
#include "FoliageType.h"
#include "InstancedFoliage.h" // FFoliageInfo

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	/** Find the UFoliageType that owns the given component using only public API.
	 *  AInstancedFoliageActor::FoliageInfos is private in UE 5.6, but the actor
	 *  exposes a const accessor `GetFoliageInfos()` we can iterate.
	 *  FFoliageInfo::GetComponent() returns the HISM that backs the foliage type;
	 *  comparing it against the hit FISMC (which is-a HISM) recovers the type.
	 *  Cost is O(N) over foliage types on the level (typically <50). */
	UFoliageType* FindFoliageTypeForComponent(AInstancedFoliageActor& IFA, UFoliageInstancedStaticMeshComponent& Component)
	{
		const TMap<UFoliageType*, TUniqueObj<FFoliageInfo>>& Infos = IFA.GetFoliageInfos();
		for (const auto& Pair : Infos)
		{
			if (Pair.Value->GetComponent() == &Component)
			{
				return Pair.Key;
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

	// --- 2. Resolve FoliageType and check it's registered for conversion ---
	UFoliageType* FoliageType = FindFoliageTypeForComponent(*IFA, *FISMC);
	if (!FoliageType)
	{
		return nullptr;
	}

	const UEMFFoliageSettings* Settings = GetDefault<UEMFFoliageSettings>();
	if (!Settings)
	{
		return nullptr;
	}

	const FEMFFoliageEntry* Entry = Settings->FindEntryForFoliageType(FoliageType);
	if (!Entry)
	{
		return nullptr;
	}

	// Damage gate (0 disables gating)
	if (Entry->MinDamageToConvert > 0.0f && DamageDealt < Entry->MinDamageToConvert)
	{
		return nullptr;
	}

	UClass* ResolvedPropClass = Entry->PropClass.LoadSynchronous();
	if (!ResolvedPropClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[FOLIAGE_CONVERT] Entry for %s has no PropClass set"),
			*GetNameSafe(FoliageType));
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
	AEMFPhysicsProp* SpawnedProp = World->SpawnActorDeferred<AEMFPhysicsProp>(
		ResolvedPropClass,
		InstanceTransform,
		/*Owner=*/nullptr,
		/*Instigator=*/nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

	if (!SpawnedProp)
	{
		UE_LOG(LogTemp, Warning, TEXT("[FOLIAGE_CONVERT] SpawnActorDeferred failed for class %s"),
			*GetNameSafe(ResolvedPropClass));
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
