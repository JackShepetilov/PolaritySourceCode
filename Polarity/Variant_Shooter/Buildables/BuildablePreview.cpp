// BuildablePreview.cpp

#include "BuildablePreview.h"

#include "BuildableActor.h"
#include "BuildableDefinition.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"

ABuildablePreview::ABuildablePreview()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	SetCanBeDamaged(false);

	USceneComponent* const Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Root);
	// A picture, not a thing: nothing may bump into it, and the placement test must not find it.
	Mesh->SetCollisionProfileName(TEXT("NoCollision"));
	Mesh->SetGenerateOverlapEvents(false);
	Mesh->SetCanEverAffectNavigation(false);
	Mesh->SetCastShadow(false);
}

void ABuildablePreview::SetupFromDefinition(const UBuildableDefinition* Definition)
{
	const ABuildableActor* const CDO = (Definition && Definition->ActorClass)
		? Definition->ActorClass->GetDefaultObject<ABuildableActor>() : nullptr;
	const UStaticMeshComponent* const Source = CDO ? CDO->GetMesh() : nullptr;
	if (!Mesh || !Source)
	{
		return;
	}
	Mesh->SetStaticMesh(Source->GetStaticMesh());
	Mesh->SetRelativeTransform(Source->GetRelativeTransform());
	// A new mesh brings its own materials back; the ghost look has to be written again.
	bMaterialApplied = false;
	SetValid(true);
}

void ABuildablePreview::SetValid(bool bValid)
{
	if (bMaterialApplied && bValid == bLastValid)
	{
		return;
	}
	bLastValid = bValid;
	bMaterialApplied = true;
	ApplyMaterial(bValid ? ValidMaterial : InvalidMaterial);
}

void ABuildablePreview::ApplyMaterial(UMaterialInterface* Material)
{
	if (!Mesh || !Material)
	{
		return;
	}
	const int32 Slots = Mesh->GetNumMaterials();
	for (int32 Index = 0; Index < Slots; ++Index)
	{
		Mesh->SetMaterial(Index, Material);
	}
}
