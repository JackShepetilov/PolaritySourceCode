// NitroGateSubsystem.cpp

#include "NitroGateSubsystem.h"
#include "NitroGate.h"

void UNitroGateSubsystem::RegisterGate(ANitroGate* Gate)
{
	if (!Gate)
	{
		return;
	}

	Gates.AddUnique(Gate);
}

void UNitroGateSubsystem::UnregisterGate(ANitroGate* Gate)
{
	if (!Gate)
	{
		return;
	}

	Gates.RemoveAll([Gate](const TWeakObjectPtr<ANitroGate>& Entry)
	{
		return !Entry.IsValid() || Entry.Get() == Gate;
	});
}

ANitroGate* UNitroGateSubsystem::FindGateUnder(const FVector& Feet) const
{
	for (int32 Index = Gates.Num() - 1; Index >= 0; --Index)
	{
		ANitroGate* Gate = Gates[Index].Get();
		if (!Gate)
		{
			Gates.RemoveAtSwap(Index);
			continue;
		}

		if (Gate->IsOnPad(Feet))
		{
			return Gate;
		}
	}

	return nullptr;
}
