// DroneTestSpawner.h
// The drone test room's spawner. Everything lives in AKeepAliveSpawner; this class stays only so the
// actor already placed in L_DroneTestRoom keeps its class and its settings.

#pragma once

#include "CoreMinimal.h"
#include "KeepAliveSpawner.h"
#include "DroneTestSpawner.generated.h"

UCLASS()
class POLARITY_API ADroneTestSpawner : public AKeepAliveSpawner
{
	GENERATED_BODY()
};
