// SaveGameSubsystem.h
// Sole owner of disk I/O for Polarity. Two physical slots:
//   - "Polarity_Meta": durable profile (currency, unlocks, tutorial flag, desktop apps, chat read-state).
//   - "Polarity_Run" : volatile mid-run resume (created on StartRun, deleted on death/victory).
//
// Lifecycle contract (avoids the InitializeDependency cycle):
//   * THIS subsystem depends on NOBODY. Initialize() only loads the meta file into memory.
//   * Gameplay subsystems (Stream/Lore) call InitializeDependency(USaveGameSubsystem) and PULL
//     their slice from GetMeta() at the end of their own Initialize().
//   * GatherMeta()/GatherRun() read the live subsystems via their public getters AT SAVE TIME
//     (always post-init, so no reentrancy).
// Logging tag: [SAVE_DEBUG].

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "SaveGameSubsystem.generated.h"

class UPolarityMetaSave;
class UPolarityRunSave;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnDesktopStateChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSaveLoaded);

UCLASS()
class POLARITY_API USaveGameSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Live, in-memory meta authority. Read-only access for scatter-pull by other subsystems. */
	UPolarityMetaSave* GetMeta() const;

	// ============ Meta lifecycle ============

	/** Immediate synchronous flush. Use on quit / high-value transitions. */
	UFUNCTION(BlueprintCallable, Category = "Save|Meta")
	void SaveMetaNow();

	/** Mark dirty + schedule a debounced flush (~3s). Use on the hot path (chat read, unlocks). */
	UFUNCTION(BlueprintCallable, Category = "Save|Meta")
	void RequestMetaSave();

	/** "Delete my progress": clears the meta slot (+ backup) and resets in-memory meta to fresh. */
	UFUNCTION(BlueprintCallable, Category = "Save")
	void WipeMeta();

	// ============ Player stream handle (asked on first launch; empty == unset) ============

	UFUNCTION(BlueprintPure, Category = "Save|Player")
	bool HasPlayerStreamerName() const;

	UFUNCTION(BlueprintPure, Category = "Save|Player")
	FString GetPlayerStreamerName() const;

	/** Sets the handle on meta + the live StreamSubsystem and persists it. */
	UFUNCTION(BlueprintCallable, Category = "Save|Player")
	void SetPlayerStreamerName(const FString& InName);

	// ============ Tutorial — called by the TUTORIAL LEVEL BP/trigger (NOT UTutorialSubsystem) ============

	/** Idempotent. First call: sets bTutorialCompleted, unlocks the "raid" app + "post_tutorial"
	 *  chat group, flushes synchronously, broadcasts OnDesktopStateChanged. Re-entry: no-op. */
	UFUNCTION(BlueprintCallable, Category = "Save|Tutorial")
	void NotifyTutorialCompleted();

	UFUNCTION(BlueprintPure, Category = "Save|Tutorial")
	bool IsTutorialCompleted() const;

	// ============ Desktop apps ============

	UFUNCTION(BlueprintPure, Category = "Save|Desktop")
	bool IsAppUnlocked(FName AppId) const;

	UFUNCTION(BlueprintCallable, Category = "Save|Desktop")
	void UnlockApp(FName AppId);

	UFUNCTION(BlueprintPure, Category = "Save|Desktop")
	TSet<FName> GetUnlockedApps() const;

	// ============ Gamer-chat read-state (group-based; index-free) ============

	UFUNCTION(BlueprintPure, Category = "Save|Chat")
	TSet<FName> GetUnlockedChatGroups() const;

	/** True the FIRST time a group is viewed (unlocked AND not yet seen) -> play with delays. */
	UFUNCTION(BlueprintPure, Category = "Save|Chat")
	bool ShouldPlayChatRealtime(FName GroupId) const;

	UFUNCTION(BlueprintPure, Category = "Save|Chat")
	bool IsChatGroupSeen(FName GroupId) const;

	UFUNCTION(BlueprintCallable, Category = "Save|Chat")
	void UnlockChatGroup(FName GroupId);

	/** Call after a group's real-time playback finishes — later visits dump it instantly and the
	 *  unread badge clears for that group. */
	UFUNCTION(BlueprintCallable, Category = "Save|Chat")
	void MarkChatGroupSeen(FName GroupId);

	/** Register per-group message counts (from the chat DataTable's Group column) so the unread
	 *  badge has real numbers. Transient — the desktop/chat widget calls this once on construct. */
	UFUNCTION(BlueprintCallable, Category = "Save|Chat")
	void RegisterChatGroupSizes(const TMap<FName, int32>& Sizes);

	/** Unread = sum of registered message counts over unlocked, not-yet-seen groups.
	 *  Returns 0 until RegisterChatGroupSizes is called (so empty/unknown groups never false-badge). */
	UFUNCTION(BlueprintPure, Category = "Save|Chat")
	int32 GetUnreadChatCount() const;

	UFUNCTION(BlueprintPure, Category = "Save|Chat")
	bool HasUnreadChat() const { return GetUnreadChatCount() > 0; }

	// ============ Run resume ============

	UFUNCTION(BlueprintPure, Category = "Save|Run")
	bool HasRunSave() const { return bRunSaveExists; }

	/** Arena index the cached run save would resume at (-1 if none). Reads memory, not disk. */
	UFUNCTION(BlueprintPure, Category = "Save|Run")
	int32 GetResumeArenaIndex() const { return CachedResumeArenaIndex; }

	/** Loads the run slot and scatters it into Run + XP subsystems. False if nothing to resume. */
	UFUNCTION(BlueprintCallable, Category = "Save|Run")
	bool ResumeRun();

	/** Checkpoint the current run to disk (StartRun / per-arena / quit-to-menu). */
	UFUNCTION(BlueprintCallable, Category = "Save|Run")
	void SaveRun();

	/** Delete the run slot (death / victory / explicit abandon). */
	UFUNCTION(BlueprintCallable, Category = "Save|Run")
	void ClearRun();

	// ============ UI change notifications (event-driven; no per-frame polling) ============

	UPROPERTY(BlueprintAssignable, Category = "Save")
	FOnDesktopStateChanged OnDesktopStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Save")
	FOnSaveLoaded OnSaveLoaded;

private:
	// ---- In-memory authority ----
	UPROPERTY()
	TObjectPtr<UPolarityMetaSave> Meta = nullptr;

	bool bMetaLoaded = false;      // GUARD: never save before the initial load completes
	bool bMetaDirty  = false;
	bool bRunSaveExists = false;
	int32 CachedResumeArenaIndex = -1;

	FTSTicker::FDelegateHandle FlushTickerHandle;
	FDelegateHandle DeactivateHandle;
	FDelegateHandle TerminateHandle;

	/** Per-group message counts, registered at runtime from the chat DataTable's Group column.
	 *  Transient (rebuilt each launch); drives GetUnreadChatCount(). */
	TMap<FName, int32> ChatGroupSizes;

	// ---- Slot names (PIE-isolated so editor testing never stomps a packaged profile) ----
	static FString MetaSlotName();
	static FString RunSlotName();
	static FString SlotFilePath(const FString& Slot);

	// ---- Fresh-save initialization (single source of truth; used on no-file boot AND WipeMeta) ----
	void InitializeFreshMeta();

	// ---- Gather (read live subsystems -> Meta/RunSave) ----
	void GatherMeta();
	void GatherRun(UPolarityRunSave& Out) const;

	// ---- Scatter (RunSave -> live subsystems). Meta scatter is pull-based by each subsystem. ----
	void ApplyRun(const UPolarityRunSave& In);

	// ---- Versioning ----
	bool MigrateMeta(UPolarityMetaSave& S) const; // false => quarantine (too new)
	bool MigrateRun(UPolarityRunSave& S) const;

	// ---- Disk plumbing (PC/desktop file-based; swap to ISaveGameSystem for console) ----
	void LoadMetaSynchronous();
	bool OnFlushTick(float);
	void FlushMetaIfDirty();
	static bool WriteSaveToDisk(class USaveGame* Obj, const FString& Slot);
	static class USaveGame* LoadSaveFromDisk(const FString& Slot);
	void BindAppLifecycleDelegates();
};
