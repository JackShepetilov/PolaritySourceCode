// ChatScript.h
// PrimaryDataAsset describing scripted chat lines.
// Phase 5 consumes this for ambient chat. Reactive lines (responses to gameplay events)
// are layered on later — same asset, additional table in a future patch.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ChatScript.generated.h"

USTRUCT(BlueprintType)
struct FChatLine
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chat")
	FString Username;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chat")
	FText Message;

	/** Username tint. White by default; designer can color-code regulars / mods / subs. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chat")
	FColor UsernameColor = FColor::White;
};

UCLASS(BlueprintType)
class POLARITY_API UChatScript : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Pool of chat lines spawned at random while no specific reaction is pending. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chat")
	TArray<FChatLine> AmbientLines;

	/** Average seconds between ambient line spawns. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chat")
	float AmbientIntervalSeconds = 1.5f;

	/** ±jitter applied to ambient interval. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chat")
	float AmbientIntervalJitter = 0.5f;
};
