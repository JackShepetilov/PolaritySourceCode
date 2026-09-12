// DroneTestModeSelector.h
// Named debug modes for a test room: which KeepAlive spawners run, how many they hold, of what.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Templates/SubclassOf.h"
#include "DroneTestModeSelector.generated.h"

class AKeepAliveSpawner;
class AShooterNPC;

/** One spawner's settings inside a mode. */
USTRUCT(BlueprintType)
struct FDroneTestSpawnerSetup
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Mode")
	TObjectPtr<AKeepAliveSpawner> Spawner;

	/** How many this spawner keeps alive in this mode. */
	UPROPERTY(EditAnywhere, Category = "Mode", meta = (ClampMin = "0", ClampMax = "20"))
	int32 KeepAlive = 1;

	/** What it spawns in this mode. None = the class set on the spawner itself. */
	UPROPERTY(EditAnywhere, Category = "Mode")
	TSubclassOf<AShooterNPC> DroneClass;
};

USTRUCT(BlueprintType)
struct FDroneTestMode
{
	GENERATED_BODY()

	/** What to type into polarity.dronetest.mode. */
	UPROPERTY(EditAnywhere, Category = "Mode")
	FName Name;

	/** Spawners this mode runs. Every KeepAlive spawner in the level that is NOT listed here is set
	 *  to keep zero, so a mode only has to name what it wants. */
	UPROPERTY(EditAnywhere, Category = "Mode")
	TArray<FDroneTestSpawnerSetup> Spawners;
};

/**
 * Place one in a test level, list the modes, pick ActiveMode. It writes KeepAlive and DroneClass
 * into every AKeepAliveSpawner before any of them begins play, so the level starts straight in the
 * chosen mode. In play, ApplyMode (or the console: polarity.dronetest.mode <name|index>) despawns
 * what the old mode had alive and fills up the new one. Server only, the spawners are too.
 */
UCLASS()
class POLARITY_API ADroneTestModeSelector : public AActor
{
	GENERATED_BODY()

public:

	ADroneTestModeSelector();

	UPROPERTY(EditAnywhere, Category = "Mode")
	TArray<FDroneTestMode> Modes;

	/** Index into Modes the level starts in. */
	UPROPERTY(EditAnywhere, Category = "Mode", meta = (ClampMin = "0"))
	int32 ActiveMode = 0;

	/** Switch modes. In play this despawns the old mode's NPCs first. False for a bad index. */
	UFUNCTION(BlueprintCallable, Category = "Mode")
	bool ApplyMode(int32 Index);

	UFUNCTION(BlueprintCallable, Category = "Mode")
	bool ApplyModeByName(FName Name);

	UFUNCTION(BlueprintPure, Category = "Mode")
	int32 GetActiveMode() const { return ActiveMode; }

	/** "1 CarriersOnly" per line, the active one marked. For the console. */
	FString DescribeModes() const;

protected:

	/** Runs for every level actor before any BeginPlay, which is when the spawners fill up. */
	virtual void PostInitializeComponents() override;

private:

	void Apply(const FDroneTestMode& Mode, bool bInPlay);

	/** Each spawner's own DroneClass, taken the first time a mode touches it, so a mode with no
	 *  override gets the level's setting back rather than the previous mode's. */
	TMap<TWeakObjectPtr<AKeepAliveSpawner>, TSubclassOf<AShooterNPC>> OriginalClasses;
};
