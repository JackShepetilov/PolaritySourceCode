// VFXVariantSequenceSubsystem.cpp

#include "VFXVariantSequenceSubsystem.h"

#include "Engine/World.h"
#include "NiagaraComponent.h"
#include "NiagaraParameterStore.h"
#include "NiagaraSystem.h"
#include "NiagaraUserRedirectionParameterStore.h"

namespace
{
	const FName VariantCountParameter(TEXT("VariantCount"));
	const FName VariantRecentWindowParameter(TEXT("VariantRecentWindow"));
	const FName VariantIndexParameter(TEXT("VariantIndex"));
}

void UVFXVariantSequenceSubsystem::Deinitialize()
{
	SequenceStates.Empty();
	Super::Deinitialize();
}

bool UVFXVariantSequenceSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld();
}

bool UVFXVariantSequenceSubsystem::ConfigureVariantForComponent(UNiagaraComponent* Component)
{
	if (!IsValid(Component))
	{
		return false;
	}

	UNiagaraSystem* System = Component->GetAsset();
	if (!IsValid(System))
	{
		return false;
	}

	float VariantCountValue = 0.0f;
	float RecentWindowValue = 0.0f;
	if (!TryGetSystemFloatParameter(System, VariantCountParameter, VariantCountValue)
		|| !TryGetSystemFloatParameter(System, VariantRecentWindowParameter, RecentWindowValue))
	{
		return false;
	}

	const int32 VariantCount = FMath::Max(0, FMath::RoundToInt(VariantCountValue));
	if (VariantCount <= 0)
	{
		return false;
	}

	const int32 RecentWindow = FMath::Clamp(
		FMath::RoundToInt(RecentWindowValue),
		0,
		FMath::Max(0, VariantCount - 1));

	const int32 VariantIndex = DrawVariant(System, VariantCount, RecentWindow);
	if (VariantIndex == INDEX_NONE)
	{
		return false;
	}

	Component->SetFloatParameter(VariantIndexParameter, static_cast<float>(VariantIndex));
	return true;
}

bool UVFXVariantSequenceSubsystem::TryGetSystemFloatParameter(
	const UNiagaraSystem* System,
	FName ParameterName,
	float& OutValue)
{
	if (!System)
	{
		return false;
	}

	const FNiagaraUserRedirectionParameterStore& Store = System->GetExposedParameters();
	FNiagaraVariableBase Variable(FNiagaraTypeDefinition::GetFloatDef(), ParameterName);
	if (!Store.RedirectUserVariable(Variable) || Store.IndexOf(Variable) == INDEX_NONE)
	{
		return false;
	}

	OutValue = Store.GetParameterValueOrDefault<float>(Variable, 0.0f);
	return true;
}

int32 UVFXVariantSequenceSubsystem::DrawVariant(
	const UNiagaraSystem* System,
	int32 VariantCount,
	int32 RecentWindow)
{
	if (!System || VariantCount <= 0)
	{
		return INDEX_NONE;
	}

	const FName SequenceKey(*System->GetPathName());
	FVariantSequenceState& State = SequenceStates.FindOrAdd(SequenceKey);
	if (State.VariantCount != VariantCount)
	{
		State = FVariantSequenceState();
		State.VariantCount = VariantCount;
	}

	while (State.Recent.Num() > RecentWindow)
	{
		State.Recent.RemoveAt(0, 1, EAllowShrinking::No);
	}

	if (State.Bag.IsEmpty())
	{
		State.Bag.Reserve(VariantCount);
		for (int32 Candidate = 0; Candidate < VariantCount; ++Candidate)
		{
			if (!State.Recent.Contains(Candidate))
			{
				State.Bag.Add(Candidate);
			}
		}

		// Defensive fallback for malformed settings. Normally RecentWindow is
		// clamped below VariantCount, so at least one candidate is available.
		if (State.Bag.IsEmpty())
		{
			for (int32 Candidate = 0; Candidate < VariantCount; ++Candidate)
			{
				State.Bag.Add(Candidate);
			}
		}

		for (int32 Index = State.Bag.Num() - 1; Index > 0; --Index)
		{
			State.Bag.Swap(Index, FMath::RandRange(0, Index));
		}
	}

	const int32 Selected = State.Bag.Pop(EAllowShrinking::No);
	if (RecentWindow > 0)
	{
		State.Recent.Add(Selected);
		while (State.Recent.Num() > RecentWindow)
		{
			State.Recent.RemoveAt(0, 1, EAllowShrinking::No);
		}
	}
	else
	{
		State.Recent.Reset();
	}

	return Selected;
}
