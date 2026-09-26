// DispenserSlotMachineComponent.h
// The dispenser's slot machine. A gun is fed in; its rounds are the stake; three reels stop one by
// one, each on its own outcome (nothing, a small buff, an upgrade of some rarity); the boxes under
// the reels open with the items inside; the player who spun takes one with the grapple and the other
// boxes close. Three blanks is the middle finger.
//
// Docs/Dispenser_Upgrade_SlotMachine_Spec_2026-09-25.md (rules), Docs/Dispenser_SlotMachine_3D_Handoff
// _2026-09-25.md (what the model shows). Server decides everything; the spin replicates to every
// machine, because everyone near the dispenser sees the reels and the items, and only the spinner
// may take one.
//
// On every buildable (ABuildableActor makes it), inert unless the building is a dispenser: the
// project's dispenser Blueprint derives from ABuildableActor directly, and the curves here have to
// be editable on it.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Curves/CurveFloat.h"
#include "Polarity/Upgrades/UpgradeManagerComponent.h"
#include "DispenserSlotMachineComponent.generated.h"

class ADispenserCardPickup;
class APlayerState;
class AShooterCharacter;
class UDispenserSlotMachineComponent;
class UDispenserBuffDefinition;
class UMaterialInstanceDynamic;
class USkeletalMeshComponent;
class UDispenserUpgradePool;
class UStaticMesh;
class UWeaponAttachmentDefinition;

/** What one reel stopped on. */
UENUM(BlueprintType)
enum class EDispenserCardOutcome : uint8
{
	Dud,
	/** A small buff: one of the pool's buff assets. */
	Buff,
	/** An upgrade level (the card decides the level and any replacement). */
	Upgrade,
	/** An attachment from the pool. */
	Attachment,
};

/** Where the machine is in one spin. */
UENUM(BlueprintType)
enum class EDispenserSpinPhase : uint8
{
	/** Ready for a gun. */
	Idle,
	/** Reels turning, boxes shut. */
	Spinning,
	/** Boxes open, waiting for the spinner to take one (or the timeout to take the worst). */
	Open,
	/** A card was taken, or all three were blanks: the boxes are shutting. */
	Closing,
};

/** One reel's result: the outcome, and what exactly it gives. */
USTRUCT(BlueprintType)
struct FDispenserCard
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Slot Machine")
	EDispenserCardOutcome Outcome = EDispenserCardOutcome::Dud;

	/** Outcome Upgrade: the card itself. */
	UPROPERTY(BlueprintReadOnly, Category = "Slot Machine")
	FUpgradeOfferCard Upgrade;

	/** Outcome Buff: index into the pool's Buffs. Outcome Attachment: into its Attachments. */
	UPROPERTY(BlueprintReadOnly, Category = "Slot Machine")
	int32 PickupIndex = INDEX_NONE;

	/** How rare the card is (an upgrade's level rarity, an attachment's own). */
	UPROPERTY(BlueprintReadOnly, Category = "Slot Machine")
	EUpgradeRarity Rarity = EUpgradeRarity::Common;

	/** Cell of the reel's symbol strip this reel stops on (the 3D handoff's table). */
	UPROPERTY(BlueprintReadOnly, Category = "Slot Machine")
	uint8 Symbol = 0;
};

/** One spin, replicated to everyone. */
USTRUCT(BlueprintType)
struct FDispenserSpin
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Slot Machine")
	EDispenserSpinPhase Phase = EDispenserSpinPhase::Idle;

	/** Who spun, and so the only one who may take a card. A player state rather than a pawn: it
	 *  outlives a death. */
	UPROPERTY(BlueprintReadOnly, Category = "Slot Machine")
	TObjectPtr<APlayerState> Spinner = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Slot Machine")
	TArray<FDispenserCard> Cards;

	UPROPERTY(BlueprintReadOnly, Category = "Slot Machine")
	int32 TakenIndex = INDEX_NONE;

	/** The stake: magazines fed in, and the value StakeCurve made of them. */
	UPROPERTY(BlueprintReadOnly, Category = "Slot Machine")
	float Magazines = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Slot Machine")
	float StakeValue = 0.0f;

	/** Server time the boxes open, and the time the machine takes the worst card itself. */
	UPROPERTY(BlueprintReadOnly, Category = "Slot Machine")
	float OpenServerTime = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Slot Machine")
	float ExpireServerTime = 0.0f;

	/** Counts spins, so a watcher can tell a new spin from the same one changing phase. */
	UPROPERTY(BlueprintReadOnly, Category = "Slot Machine")
	int32 Serial = 0;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDispenserSpinEvent, UDispenserSlotMachineComponent*, Machine);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDispenserCardEvent, UDispenserSlotMachineComponent*, Machine, int32, CardIndex);

UCLASS(ClassGroup = (Buildables), meta = (BlueprintSpawnableComponent))
class POLARITY_API UDispenserSlotMachineComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	UDispenserSlotMachineComponent();

	// ==================== Settings ====================

	/** What the machine hands out: the slots with their upgrades, and the buffs. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Slot Machine")
	TSoftObjectPtr<UDispenserUpgradePool> UpgradePool;

	/** X: magazines fed in. Y: stake value. Non-linear on purpose: two magazines are worth next to
	 *  nothing, 3-5 decent, 6-9 good, past 9 it barely grows. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Slot Machine|Stake")
	FRuntimeFloatCurve StakeCurve;

	/** X: stake value. Y: chance (0..1) that a reel is a blank. Never zero, not even at nine magazines. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Slot Machine|Outcome")
	FRuntimeFloatCurve DudChanceCurve;

	/** X: stake value. Y: chance (0..1) that a reel is a small buff. The rest of 1 is an upgrade. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Slot Machine|Outcome")
	FRuntimeFloatCurve BuffChanceCurve;

	/** An upgrade reel's rarity: stake value x PayoutCurve(roll 0..1) against the thresholds below.
	 *  X: uniform roll, Y: multiplier. Keep it rising left to right. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Slot Machine|Rarity")
	FRuntimeFloatCurve PayoutCurve;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Slot Machine|Rarity", meta = (ClampMin = "0.0"))
	float RareValue = 60.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Slot Machine|Rarity", meta = (ClampMin = "0.0"))
	float EpicValue = 120.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Slot Machine|Rarity", meta = (ClampMin = "0.0"))
	float LegendaryValue = 240.0f;

	/** Reels, boxes, cards. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Slot Machine", meta = (ClampMin = "1", ClampMax = "5"))
	int32 CardCount = 3;

	/** Seconds the reels turn before the boxes open. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Slot Machine|Timing", meta = (ClampMin = "0.0", Units = "s"))
	float SpinSeconds = 2.0f;

	/** Seconds the spinner has to take a card; then the machine takes the WORST one for them. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Slot Machine|Timing", meta = (ClampMin = "1.0", Units = "s"))
	float OfferTimeoutSeconds = 30.0f;

	/** Seconds the boxes take to shut after a card is taken. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Slot Machine|Timing", meta = (ClampMin = "0.0", Units = "s"))
	float CloseSeconds = 1.5f;

	/** Seconds the middle finger stays up after three blanks. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Slot Machine|Timing", meta = (ClampMin = "0.0", Units = "s"))
	float AllDudsSeconds = 4.0f;

	/** The box item: taken with the grapple, and wearing the look of the real pickup the card is. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Slot Machine")
	TSubclassOf<ADispenserCardPickup> CardPickupClass;

	/** Sockets on the building's skeletal mesh where the items appear, one per box. Missing socket:
	 *  the item appears in front of the building instead. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Slot Machine")
	TArray<FName> BoxSockets;

	// ==================== Model ====================
	// The machine's moving parts: the box lids, the intake flap and the reels. Worked out on every
	// machine from the replicated spin, nothing extra replicates. It needs a skeletal mesh on the
	// building carrying the lid bones (BP_Buildable_Dispenser's MachineMesh); the component finds
	// it, gives it UDispenserMachineAnimInstance and makes the reel materials dynamic. No such mesh:
	// all of this sleeps. Bones turn in component space about their own rest position, so the
	// bone axes the FBX gave do not matter (the turret's recipe).

	/** Lid bones, in box order. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Slot Machine|Model")
	TArray<FName> LidBones;

	/** The line (component space) a lid turns about, through its bone. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Slot Machine|Model")
	FVector LidHingeAxis = FVector(0.0f, 1.0f, 0.0f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Slot Machine|Model", meta = (Units = "deg"))
	float LidOpenDegrees = -90.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Slot Machine|Model", meta = (ClampMin = "0.01", Units = "s"))
	float LidMoveSeconds = 0.3f;

	/** Swings open and shut once as a spin starts (the gun going in). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Slot Machine|Model")
	FName FlapBone;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Slot Machine|Model")
	FVector FlapHingeAxis = FVector(1.0f, 0.0f, 0.0f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Slot Machine|Model", meta = (Units = "deg"))
	float FlapOpenDegrees = -80.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Slot Machine|Model", meta = (ClampMin = "0.01", Units = "s"))
	float FlapSeconds = 0.6f;

	/** Material slots of the reels, in reel order (M_DispenserReel: ReelOffset, ReelBlur,
	 *  ResultColor, ResultGlow). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Slot Machine|Model")
	TArray<FName> ReelSlots;

	/** Seconds from the start of a spin at which each reel stops (never later than SpinSeconds). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Slot Machine|Model")
	TArray<float> ReelStopSeconds;

	/** ReelOffset that puts cell 0 of the symbol strip in the window, and the step to the next
	 *  cell (negative if the strip runs the other way). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Slot Machine|Model")
	float ReelOffsetForCell0 = 0.5625f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Slot Machine|Model")
	float ReelOffsetPerCell = 0.125f;

	/** Full speed of a turning reel, in cells a second. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Slot Machine|Model", meta = (ClampMin = "0.0"))
	float ReelCellsPerSecond = 24.0f;

	/** Seconds a reel takes to slow from full speed to its stop. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Slot Machine|Model", meta = (ClampMin = "0.01", Units = "s"))
	float ReelEaseSeconds = 0.4f;

	/** ReelBlur at full speed. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Slot Machine|Model", meta = (ClampMin = "0.0"))
	float ReelSpinBlur = 0.6f;

	/** ResultGlow of a stopped reel showing a blank; anything else glows at 1. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Slot Machine|Model", meta = (ClampMin = "0.0"))
	float ReelBlankGlow = 0.15f;

	/** Total time to open the shutter and extend the tray; closing reverses the sequence. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Slot Machine|Model", meta = (ClampMin = "0.6", Units = "s"))
	float CassetteMoveSeconds = 1.15f;

	bool HasCassettes() const { return bCassetteModel; }
	float GetCassetteAlpha(int32 Index) const;
	bool IsCardPresented(int32 Index) const;
	USkeletalMeshComponent* GetMachineMesh() const { return MachineMesh; }

	/** Degrees lid Index stands at now (0 shut). What UDispenserMachineAnimInstance puts on it. */
	float GetLidAngle(int32 Index) const;

	/** Degrees the intake flap stands at now. */
	float GetFlapAngle() const { return FlapAngle; }

	// ==================== Server ====================

	/** Why Donor may not spin now (a spin already running), or empty when they may. Asked before
	 *  the gun leaves the hands. */
	FString GetSpinRefusal(const AShooterCharacter* Donor) const;

	/** A gun worth Magazines has just been fed in by Donor: roll the reels and start the spin. */
	void StartSpin(AShooterCharacter* Donor, float Magazines, const FString& WeaponName);

	/** Take card Index for Taker: from an item that reached them on the grapple, or from the menu.
	 *  False when it is not theirs, not open, or a blank. */
	bool TakeCard(int32 Index, AShooterCharacter* Taker);

	// ==================== Queries, every machine ====================

	UFUNCTION(BlueprintPure, Category = "Slot Machine")
	const FDispenserSpin& GetSpin() const { return Spin; }

	UFUNCTION(BlueprintPure, Category = "Slot Machine")
	bool IsSpinner(const AShooterCharacter* Character) const;

	/** One line for a card: "[Rare] Air Dash Lv 1 -> 2", "[Epic] Holo sight", "Health +25", "blank". */
	FText DescribeCard(int32 Index) const;

	/** The pickup class card Index is shown as in its box (an upgrade pickup, an attachment pickup),
	 *  or null: a blank, or a buff, which the box item draws with the buff's own mesh. */
	TSubclassOf<AActor> GetCardVisualClass(int32 Index) const;

	/** Card Index's attachment or buff asset, or null. */
	UWeaponAttachmentDefinition* GetCardAttachment(int32 Index) const;
	const UDispenserBuffDefinition* GetCardBuff(int32 Index) const;

	/** Every attachment Character has: in the bag, or on any of their guns. */
	static void GatherOwnedAttachments(const AShooterCharacter* Character, TArray<const UWeaponAttachmentDefinition*>& OutOwned);

	/** The pool, loaded. */
	const UDispenserUpgradePool* GetPool() const;

	static FLinearColor RarityColor(EUpgradeRarity Rarity);

	/** A card's colour: its rarity, green for a buff, grey for a blank. */
	static FLinearColor CardColor(const FDispenserCard& Card);

	/** No reel won anything. */
	bool IsAllBlanks() const;

	float EvaluateStakeValue(float Magazines) const;
	float EvaluatePayoutMultiplier(float Roll) const;
	EUpgradeRarity RarityForValue(float Value) const;

	/** One reel for Player at this stake. What it offers goes into the Used lists, so an offer shows
	 *  an upgrade or an attachment once. Server. */
	void RollCard(float StakeValue, const UDispenserUpgradePool* Pool, const AShooterCharacter* Player,
		TArray<const UUpgradeDefinition*>& UsedUpgrades, TArray<int32>& UsedAttachments, FDispenserCard& OutCard) const;

	/** Worst first: a blank, then a buff, then upgrades and attachments by rarity. */
	static int32 RankOf(const FDispenserCard& Card);

	// ==================== Events, every machine ====================
	// For the model, sound and effects: bind in the dispenser Blueprint.

	UPROPERTY(BlueprintAssignable, Category = "Slot Machine|Events")
	FOnDispenserSpinEvent OnSpinStarted;

	UPROPERTY(BlueprintAssignable, Category = "Slot Machine|Events")
	FOnDispenserSpinEvent OnBoxesOpened;

	UPROPERTY(BlueprintAssignable, Category = "Slot Machine|Events")
	FOnDispenserCardEvent OnCardTaken;

	UPROPERTY(BlueprintAssignable, Category = "Slot Machine|Events")
	FOnDispenserSpinEvent OnAllBlanks;

	UPROPERTY(BlueprintAssignable, Category = "Slot Machine|Events")
	FOnDispenserSpinEvent OnSpinClosed;

	/** Anything about the spin changed. */
	UPROPERTY(BlueprintAssignable, Category = "Slot Machine|Events")
	FOnDispenserSpinEvent OnSpinChanged;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:

	UPROPERTY(ReplicatedUsing = OnRep_Spin)
	FDispenserSpin Spin;

	UFUNCTION()
	void OnRep_Spin();

	/** Fire the events for whatever changed since the last look. Server after each change, clients
	 *  from OnRep. */
	void FireSpinEvents();

	void OpenBoxes();
	void ExpireOffer();
	void FinishClose();
	void BeginClosing();
	float GetCloseDuration() const;
	void SpawnItems();
	void DestroyItems();
	FTransform BoxTransform(int32 Index) const;
	/** An attachment card: a fresh attachment pickup flies into Taker's bag; what does not fit is put
	 *  on the floor by the pickup itself. */
	void DeliverAttachment(UWeaponAttachmentDefinition* Attachment, AShooterCharacter* Taker) const;

	/** A buff card: its effect, at once. */
	void ApplyBuff(const UDispenserBuffDefinition* Buff, AShooterCharacter* Taker) const;
	void Receipt(AShooterCharacter* Donor, const FString& Text) const;

	/** What the last FireSpinEvents saw. */
	int32 SeenSerial = 0;
	EDispenserSpinPhase SeenPhase = EDispenserSpinPhase::Idle;
	int32 SeenTaken = INDEX_NONE;

	/** Server: the items lying in the open boxes. */
	TArray<TWeakObjectPtr<ADispenserCardPickup>> Items;

	FTimerHandle PhaseTimer;

	// ==================== Model ====================

	/** Find the machine's skeletal mesh and set it up (anim class, reel materials). */
	void SetUpModel();

	/** Move the lids, flap and reels one frame. False once everything stands still. */
	bool UpdateModel(float DeltaSeconds);

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> MachineMesh;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> ReelMaterials;

	/** 0 shut .. 1 open, per lid, moving at LidMoveSeconds. */
	TArray<float> LidOpenness;

	float FlapAngle = 0.0f;
	bool bCassetteModel = false;
};
