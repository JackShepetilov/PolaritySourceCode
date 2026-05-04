// FoliageConversionLibrary.cpp

#include "FoliageConversionLibrary.h"

#include "EMFFoliageSettings.h"
#include "EMFPhysicsProp.h"

#include "InstancedFoliageActor.h"
#include "FoliageInstancedStaticMeshComponent.h"
#include "FoliageType.h"
#include "InstancedFoliage.h" // FFoliageInfo

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/HitResult.h"
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
	// Use IsA + static_cast instead of Cast<>: AInstancedFoliageActor and
	// UFoliageInstancedStaticMeshComponent are MinimalAPI, which can break
	// the template Cast<> path across modules with C2680 dynamic_cast errors.
	AActor* HitActor = Hit.GetActor();
	if (!HitActor || !HitActor->IsA(AInstancedFoliageActor::StaticClass()))
	{
		return nullptr;
	}
	AInstancedFoliageActor* IFA = static_cast<AInstancedFoliageActor*>(HitActor);

	UPrimitiveComponent* HitComp = Hit.GetComponent();
	if (!HitComp || !HitComp->IsA(UFoliageInstancedStaticMeshComponent::StaticClass()))
	{
		return nullptr;
	}
	UFoliageInstancedStaticMeshComponent* FISMC = static_cast<UFoliageInstancedStaticMeshComponent*>(HitComp);

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

	// Use StaticClass()->GetDefaultObject() instead of GetDefault<T>() —
	// the template has caused Cast<> instantiation issues alongside the
	// MinimalAPI-marked Foliage types in this TU.
	const UEMFFoliageSettings* Settings =
		static_cast<const UEMFFoliageSettings*>(UEMFFoliageSettings::StaticClass()->GetDefaultObject());
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
	//        initialize against the BP's default mesh, then need a recreate.
	//        We spawn through the AActor template path to avoid instantiating
	//        Cast<AEMFPhysicsProp> in this TU (interaction with MinimalAPI types
	//        in the same compile unit was triggering C2680 in Casts.h). ---
	AActor* SpawnedActor = World->SpawnActorDeferred<AActor>(
		ResolvedPropClass,
		InstanceTransform,
		/*Owner=*/nullptr,
		/*Instigator=*/nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

	if (!SpawnedActor)
	{
		UE_LOG(LogTemp, Warning, TEXT("[FOLIAGE_CONVERT] SpawnActorDeferred failed for class %s"),
			*GetNameSafe(ResolvedPropClass));
		return nullptr;
	}

	// Class hierarchy was already validated via TSubclassOf<AEMFPhysicsProp>
	// in the settings entry, so static_cast is safe here.
	AEMFPhysicsProp* SpawnedProp = static_cast<AEMFPhysicsProp*>(SpawnedActor);

	// Override the prop's mesh to match the foliage instance.
	// One BP_EMFProp can back many visually-different convertible foliage assets.
	if (SpawnedProp->PropMesh && InstanceMesh)
	{
		SpawnedProp->PropMesh->SetStaticMesh(InstanceMesh);
	}

	// --- 5. Remove the foliage instance BEFORE FinishSpawningActor.
	//        If we leave the instance in place during BeginPlay, the prop's
	//        physics body initializes overlapping the instance's collision —
	//        Chaos can register the resulting depenetration as immediate sleep
	//        and the body never wakes until something pushes it again.
	//        Note: HISM RemoveInstance uses RemoveAtSwap and shuffles the last
	//        instance into this slot, so any cached Hit.Item for this component
	//        from earlier in the same frame is now invalid. ---
	FISMC->RemoveInstance(InstanceIndex);

	UGameplayStatics::FinishSpawningActor(SpawnedProp, InstanceTransform);

	// --- 6. Wake the rigid body explicitly.
	//        Even with DefaultCharge==0, the caller will run ionization right
	//        after this returns and SetCharge() flips SetSimulatePhysics(true).
	//        Chaos sometimes leaves a freshly-simulating body in sleep state
	//        (zero velocity, no contacts), which makes OverlapMultiByObjectType
	//        in the channeling capture scan miss the prop until the body is
	//        woken by an external impulse (e.g. running into it). ---
	if (SpawnedProp->PropMesh)
	{
		SpawnedProp->PropMesh->WakeAllRigidBodies();
	}

	UE_LOG(LogTemp, Log, TEXT("[FOLIAGE_CONVERT] Converted instance %d of %s -> %s at %s"),
		InstanceIndex,
		*GetNameSafe(InstanceMesh),
		*GetNameSafe(SpawnedProp),
		*InstanceTransform.GetLocation().ToString());

	return SpawnedProp;
}
