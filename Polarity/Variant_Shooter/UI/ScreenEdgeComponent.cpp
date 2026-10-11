// ScreenEdgeComponent.cpp

#include "ScreenEdgeComponent.h"
#include "Components/PostProcessComponent.h"
#include "GameFramework/Pawn.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace
{
	const FName EdgeColorParam(TEXT("Color"));
	const FName EdgeIntensityParam(TEXT("Intensity"));
	constexpr float EdgeFadeInTime = 0.12f;
}

UScreenEdgeComponent::UScreenEdgeComponent()
{
	// Only while something is glowing.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

UScreenEdgeComponent* UScreenEdgeComponent::GetOrCreate(AActor* Owner)
{
	const APawn* const Pawn = Cast<APawn>(Owner);
	if (!Pawn || !Pawn->IsLocallyControlled())
	{
		return nullptr;
	}

	if (UScreenEdgeComponent* const Existing = Owner->FindComponentByClass<UScreenEdgeComponent>())
	{
		return Existing;
	}

	UScreenEdgeComponent* const Made = NewObject<UScreenEdgeComponent>(Owner);
	Made->RegisterComponent();
	return Made;
}

void UScreenEdgeComponent::EnsureVolume()
{
	if (Volume || !GetOwner())
	{
		return;
	}

	Volume = NewObject<UPostProcessComponent>(GetOwner());
	Volume->bUnbound = true;
	Volume->BlendWeight = 1.0f;
	// Above the level's own volumes, so the glow is not blended away by one of them.
	Volume->Priority = 100.0f;
	Volume->RegisterComponent();
}

void UScreenEdgeComponent::Play(FName Key, UMaterialInterface* Material, FLinearColor Color, float Duration, float FadeTime)
{
	if (!Material || Duration <= 0.0f)
	{
		return;
	}
	EnsureVolume();
	if (!Volume)
	{
		return;
	}

	FEdgeEffect* Effect = Effects.FindByPredicate([Key](const FEdgeEffect& E) { return E.Key == Key; });
	if (Effect && (!Effect->Material.IsValid() || Effect->Material->Parent != Material))
	{
		// Same key, different material: start over.
		Stop(Key, 0.0f);
		Effect = nullptr;
	}
	if (!Effect)
	{
		UMaterialInstanceDynamic* const Instance = UMaterialInstanceDynamic::Create(Material, this);
		Instances.Add(Instance);
		Volume->Settings.AddBlendable(Instance, 1.0f);

		Effect = &Effects.AddDefaulted_GetRef();
		Effect->Key = Key;
		Effect->Material = Instance;
		Effect->Intensity = 0.0f;
	}

	Effect->Material->SetVectorParameterValue(EdgeColorParam, Color);
	Effect->HoldLeft = Duration;
	Effect->FadeTime = FMath::Max(0.01f, FadeTime);
	Effect->bFadingIn = true;
	SetComponentTickEnabled(true);
}

void UScreenEdgeComponent::Stop(FName Key, float FadeTime)
{
	for (FEdgeEffect& Effect : Effects)
	{
		if (Effect.Key == Key)
		{
			Effect.HoldLeft = 0.0f;
			Effect.FadeTime = FMath::Max(0.01f, FadeTime);
			Effect.bFadingIn = false;
		}
	}
}

void UScreenEdgeComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	for (int32 Index = Effects.Num() - 1; Index >= 0; --Index)
	{
		FEdgeEffect& Effect = Effects[Index];
		UMaterialInstanceDynamic* const Instance = Effect.Material.Get();

		Effect.HoldLeft -= DeltaTime;
		if (Effect.HoldLeft > 0.0f)
		{
			Effect.Intensity = FMath::Min(1.0f, Effect.Intensity + DeltaTime / EdgeFadeInTime);
		}
		else
		{
			Effect.Intensity -= DeltaTime / Effect.FadeTime;
		}

		if (!Instance || Effect.Intensity <= 0.0f)
		{
			if (Instance && Volume)
			{
				Volume->Settings.RemoveBlendable(Instance);
			}
			Instances.Remove(Instance);
			Effects.RemoveAtSwap(Index);
			continue;
		}

		Instance->SetScalarParameterValue(EdgeIntensityParam, Effect.Intensity);
	}

	if (Effects.Num() == 0)
	{
		SetComponentTickEnabled(false);
	}
}

void UScreenEdgeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Volume)
	{
		Volume->DestroyComponent();
		Volume = nullptr;
	}
	Effects.Reset();
	Instances.Reset();

	Super::EndPlay(EndPlayReason);
}
