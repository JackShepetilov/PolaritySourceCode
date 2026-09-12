// WeaponAttachmentDefinition.cpp

#include "WeaponAttachmentDefinition.h"
#include "ShooterWeapon.h"

namespace
{
	/** How many classes deep this one sits. Used to let the closest listed ancestor win. Named for
	 *  this file on purpose: a unity build glues neighbouring files into one, anonymous namespaces
	 *  and all, and a plain "ClassDepth" elsewhere would be a redefinition. */
	int32 AttachmentClassDepth(const UClass* Class)
	{
		int32 Depth = 0;
		for (; Class; Class = Class->GetSuperClass())
		{
			++Depth;
		}
		return Depth;
	}
}

bool UWeaponAttachmentDefinition::FitsWeapon(TSubclassOf<AShooterWeapon> WeaponClass) const
{
	const UClass* Weapon = WeaponClass.Get();
	if (!Weapon)
	{
		return false;
	}

	// A magazine's list IS its size table. Asking the table keeps one answer for both questions.
	if (Type == EWeaponAttachmentType::Magazine)
	{
		return GetMagazineSizeFor(WeaponClass) > 0;
	}

	for (const TSubclassOf<AShooterWeapon>& Allowed : CompatibleWeapons)
	{
		if (const UClass* AllowedClass = Allowed.Get(); AllowedClass && Weapon->IsChildOf(AllowedClass))
		{
			return true;
		}
	}

	// Empty list, or not on it. Either way: no. @see CompatibleWeapons
	return false;
}

int32 UWeaponAttachmentDefinition::GetMagazineSizeFor(TSubclassOf<AShooterWeapon> WeaponClass) const
{
	const UClass* Weapon = WeaponClass.Get();
	if (!Weapon || Type != EWeaponAttachmentType::Magazine)
	{
		return 0;
	}

	// Walked rather than looked up, because a listed parent counts for its children and the map can
	// only find exact keys. When both a gun and its parent are listed, the deeper one is the more
	// specific answer and wins: "every rifle 40, this one 45" works as it reads.
	int32 BestSize = 0;
	int32 BestDepth = -1;

	for (const TPair<TSubclassOf<AShooterWeapon>, int32>& Entry : MagazineSizeByWeapon)
	{
		const UClass* Listed = Entry.Key.Get();
		if (!Listed || Entry.Value <= 0 || !Weapon->IsChildOf(Listed))
		{
			continue;
		}

		const int32 Depth = AttachmentClassDepth(Listed);
		if (Depth > BestDepth)
		{
			BestDepth = Depth;
			BestSize = Entry.Value;
		}
	}

	return FMath::Min(BestSize, 999);
}
