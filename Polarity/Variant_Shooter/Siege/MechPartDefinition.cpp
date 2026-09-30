// MechPartDefinition.cpp

#include "MechPartDefinition.h"

FString UMechPartDefinition::SlotName(EMechPartSlot InSlot)
{
	const UEnum* const Enum = StaticEnum<EMechPartSlot>();
	return Enum ? Enum->GetNameStringByValue(static_cast<int64>(InSlot)) : TEXT("?");
}
