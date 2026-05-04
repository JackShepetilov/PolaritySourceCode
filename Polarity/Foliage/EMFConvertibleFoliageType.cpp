// EMFConvertibleFoliageType.cpp

#include "EMFConvertibleFoliageType.h"

UEMFConvertibleFoliageType::UEMFConvertibleFoliageType()
{
	// Convertible foliage must be hittable by visibility traces by default.
	// Users can still tune per-asset, but the defaults shouldn't be NoCollision —
	// that would silently break hit-detection of the conversion path.
	BodyInstance.SetCollisionProfileName(TEXT("BlockAll"));
}
