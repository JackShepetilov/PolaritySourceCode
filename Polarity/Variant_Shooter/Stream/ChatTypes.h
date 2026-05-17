// ChatTypes.h
// Shared types for the chat system: enums, DataTable row structs, output message struct, delegates.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"
#include "ChatTypes.generated.h"

UENUM(BlueprintType)
enum class EChatMessageKind : uint8
{
	Ambient,
	Reaction,
	Scripted,
	Hint,
	Channel,
	Hype,
	Boredom,
	Schadenfreude,
	DirectMention,
	FriendEcho,
	Debug
};

// ==================== DataTable Rows ====================

USTRUCT(BlueprintType)
struct FChatPersonaRow : public FTableRowBase
{
	GENERATED_BODY()

	/** Displayed username. RowName of the row is the ID used by other tables to reference this persona. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chat")
	FString Username;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chat")
	FColor Color = FColor::White;

	/** Cosmetic tag describing the persona's vibe — toxic_helpful, bot, fan, etc. Reserved for future filtering. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chat")
	FName PersonalityTag;
};

USTRUCT(BlueprintType)
struct FChatAmbientRow : public FTableRowBase
{
	GENERATED_BODY()

	/** RowName in DT_ChatPersonas. If valid, Username and Color come from there. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chat")
	FName PersonaRow;

	/** If PersonaRow is empty, this overrides; if both empty, broker picks random nick + color. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chat")
	FString UsernameOverride;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chat")
	FText Message;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chat", meta = (ClampMin = "0.0"))
	float Weight = 1.0f;
};

USTRUCT(BlueprintType)
struct FChatReactionRow : public FTableRowBase
{
	GENERATED_BODY()

	/** EventTag drives selection — broker collects rows matching the fired tag, then weighted-picks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chat")
	FGameplayTag EventTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chat")
	FName PersonaRow;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chat")
	FString UsernameOverride;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chat")
	FText Message;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chat", meta = (ClampMin = "0.0"))
	float Weight = 1.0f;
};

USTRUCT(BlueprintType)
struct FChatScriptedRow : public FTableRowBase
{
	GENERATED_BODY()

	/** Multiple rows with the same SequenceID form one scripted sequence (ordered by StepIndex). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chat")
	FName SequenceID;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chat")
	int32 StepIndex = 0;

	/** Seconds to wait before this step fires (relative to the previous step in the sequence). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chat", meta = (ClampMin = "0.0"))
	float DelaySec = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chat")
	FName PersonaRow;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chat")
	FString UsernameOverride;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chat")
	FText Message;
};

USTRUCT(BlueprintType)
struct FChatHintRow : public FTableRowBase
{
	GENERATED_BODY()

	/** Matches ESkillCategory enum name (Melee/Weapon/EMF/Movement). Empty = generic hint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chat")
	FName XPUnderUsedCategory;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chat")
	FName PersonaRow;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chat")
	FString UsernameOverride;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chat")
	FText Message;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chat", meta = (ClampMin = "0.0"))
	float Weight = 1.0f;
};

// ==================== Output Message ====================

USTRUCT(BlueprintType)
struct FChatMessage
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Chat")
	FString Username;

	UPROPERTY(BlueprintReadOnly, Category = "Chat")
	FText Message;

	UPROPERTY(BlueprintReadOnly, Category = "Chat")
	FColor UsernameColor = FColor::White;

	UPROPERTY(BlueprintReadOnly, Category = "Chat")
	EChatMessageKind Kind = EChatMessageKind::Ambient;
};

// ==================== Delegates ====================

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnChatMessageReady, const FChatMessage&, Message);
