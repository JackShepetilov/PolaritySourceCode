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
	FText AuthorName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FText MessageText;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FText DateTime;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool IsNewAuthor = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float DelayBefore = 0.0f;

	/** Conversation group this row belongs to, e.g. "intro" / "post_tutorial". Drives chat
	 *  unlocking + the unread badge. Rows are filtered/counted by Group, NOT by RowName, so rows
	 *  can be inserted anywhere with any unique RowName without breaking read-state. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName Group;
};
