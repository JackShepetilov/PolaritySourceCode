// ChatMessageTypes.h
// Data types for the in-game chat UI (main menu gamer chat)

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "ChatMessageTypes.generated.h"

USTRUCT(BlueprintType)
struct FChatMessage : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString AuthorName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString MessageText;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString DateTime;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool IsNewAuthor = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float DelayBefore = 0.0f;
};
