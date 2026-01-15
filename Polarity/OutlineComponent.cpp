// OutlineComponent.cpp

#include "OutlineComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"

UOutlineComponent::UOutlineComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UOutlineComponent::BeginPlay()
{
	Super::BeginPlay();

	UpdateMeshCache();

	if (bOutlineEnabled && OutlineType != EOutlineType::None)
	{
		ApplyStencilToMeshes();
	}
}

void UOutlineComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	RemoveStencilFromMeshes();
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

#if WITH_EDITOR
void UOutlineComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.GetPropertyName();

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UOutlineComponent, OutlineType) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(UOutlineComponent, bOutlineEnabled))
	{
		// В редакторе тоже обновляем для превью
		if (GetOwner())
		{
			UpdateMeshCache();
			if (bOutlineEnabled && OutlineType != EOutlineType::None)
			{
				ApplyStencilToMeshes();
			}
			else
			{
				RemoveStencilFromMeshes();
			}
		}
	}
}
#endif

void UOutlineComponent::EnableOutline()
{
	if (bOutlineEnabled)
	{
		return;
	}

	bOutlineEnabled = true;

	if (OutlineType != EOutlineType::None)
	{
		ApplyStencilToMeshes();
	}
}

void UOutlineComponent::DisableOutline()
{
	if (!bOutlineEnabled)
	{
		return;
	}

	bOutlineEnabled = false;
	RemoveStencilFromMeshes();
}

void UOutlineComponent::SetOutlineType(EOutlineType NewType)
{
	if (OutlineType == NewType)
	{
		return;
	}

	OutlineType = NewType;

	if (bOutlineEnabled)
	{
		if (OutlineType != EOutlineType::None)
		{
			ApplyStencilToMeshes();
		}
		else
		{
			RemoveStencilFromMeshes();
		}
	}
}

bool UOutlineComponent::IsVisibleThroughWalls() const
{
	return OutlineType == EOutlineType::Destroy || OutlineType == EOutlineType::Interact;
}

int32 UOutlineComponent::GetStencilValue() const
{
	return static_cast<int32>(OutlineType);
}

void UOutlineComponent::UpdateMeshCache()
{
	CachedMeshComponents.Reset();

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	// Собираем все Primitive компоненты (StaticMesh, SkeletalMesh, и т.д.)
	TArray<UPrimitiveComponent*> PrimitiveComponents;
	Owner->GetComponents<UPrimitiveComponent>(PrimitiveComponents);

	for (UPrimitiveComponent* Primitive : PrimitiveComponents)
	{
		// Фильтруем только рендерящиеся меши
		if (Primitive && Primitive->IsVisible())
		{
			CachedMeshComponents.Add(Primitive);
		}
	}
}

void UOutlineComponent::ApplyStencilToMeshes()
{
	if (CachedMeshComponents.Num() == 0)
	{
		UpdateMeshCache();
	}

	const int32 StencilValue = GetStencilValue();

	for (TWeakObjectPtr<UPrimitiveComponent>& WeakMesh : CachedMeshComponents)
	{
		if (UPrimitiveComponent* Mesh = WeakMesh.Get())
		{
			Mesh->SetRenderCustomDepth(true);
			Mesh->SetCustomDepthStencilValue(StencilValue);
			Mesh->SetCustomDepthStencilWriteMask(ERendererStencilMask::ERSM_Default);
		}
	}
}

void UOutlineComponent::RemoveStencilFromMeshes()
{
	for (TWeakObjectPtr<UPrimitiveComponent>& WeakMesh : CachedMeshComponents)
	{
		if (UPrimitiveComponent* Mesh = WeakMesh.Get())
		{
			Mesh->SetRenderCustomDepth(false);
			Mesh->SetCustomDepthStencilValue(0);
		}
	}
}
