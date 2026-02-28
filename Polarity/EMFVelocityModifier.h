// EMFVelocityModifier.h
// Integrates EMF_Plugin electromagnetic forces with ApexMovementComponent

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VelocityModifier.h"
#include "EMFVelocityModifier.generated.h"

class UApexMovementComponent;
class UEMF_FieldComponent;
class AEMFChannelingPlateActor;

// EMF Plugin - forward declaration
struct FEMSourceDescription;

// Делегаты
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnChargeChanged, float, NewCharge);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnChargeNeutralized, AActor*, OtherActor, float, PreviousCharge);

/**
 * Компонент интеграции электромагнитных сил с системой движения.
 * Работает совместно с UEMF_FieldComponent для получения параметров заряда.
 * Реализует IVelocityModifier для применения силы Лоренца через ApexMovementComponent.
 *
 * Использование:
 * 1. Добавить UEMF_FieldComponent к персонажу (для заряда и полей)
 * 2. Добавить EMFVelocityModifier (для интеграции с движением)
 * 3. Компонент автоматически находит UEMF_FieldComponent и регистрируется в ApexMovementComponent
 */
UCLASS(ClassGroup = (EMF), meta = (BlueprintSpawnableComponent))
class POLARITY_API UEMFVelocityModifier : public UActorComponent, public IVelocityModifier
{
	GENERATED_BODY()

public:
	UEMFVelocityModifier();

	// ==================== EMF Parameters ====================

	/** Максимальная сила, которую можно применить (для предотвращения экстремальных значений) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF")
	float MaxForce = 100000.0f;

	/** Включить EMF эффекты на движение */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF")
	bool bEnabled = true;

	/** Maximum distance to consider EMF sources (sources further than this are ignored for performance) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Performance", meta = (ClampMin = "100.0", Units = "cm"))
	float MaxSourceDistance = 10000.0f;

	// ==================== Charge Accumulation ====================

	/** Текущий заряд (модуль). Расходуется на выстрелы и способности. Начинается с 0. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Charge Accumulation")
	float BaseCharge = 0.0f;

	/** Максимальный заряд */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Charge Accumulation", meta = (ClampMin = "0.0"))
	float MaxBaseCharge = 50.0f;

	/** Заряд, добавляемый за каждый успешный удар в ближнем бою */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Charge Accumulation", meta = (ClampMin = "0.0"))
	float ChargePerMeleeHit = 2.0f;

	/** Знак полярности (+1 или -1). Не зависит от величины заряда. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Charge Accumulation")
	int32 ChargeSign = 1;

	/** Максимальный бонусный заряд (legacy, не используется — единый пул) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Charge Accumulation", meta = (ClampMin = "0.0"))
	float MaxBonusCharge = 0.0f;

	/** Скорость убывания бонусного заряда (legacy, 0 = без убывания) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Charge Accumulation", meta = (ClampMin = "0.0"))
	float BonusChargeDecayRate = 0.0f;

	// ==================== Charge Neutralization ====================

	/** Разрешить нейтрализацию заряда при контакте с противоположным */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Neutralization")
	bool bCanNeutralizeOnContact = true;

	/** Нейтрализовать только себя, не трогая цель (для объектов в мире) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Neutralization")
	bool bNeutralizeTargetOnly = false;

	/** Минимальный заряд у другого объекта для нейтрализации */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Neutralization")
	float MinChargeToNeutralize = 1.0f;

	/** Время неуязвимости после нейтрализации (сек) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Neutralization")
	float NeutralizationCooldown = 1.0f;

	// ==================== Force Filtering ====================

	/** Multiplier for forces from NPC/Enemy sources.
	 *  0.0 = ignore NPC forces, 1.0 = full effect, >1.0 = amplified, <0.0 = inverted
	 *  NOT clamped - allows negative values and values > 1.0 for gameplay flexibility */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Force Filtering")
	float NPCForceMultiplier = 1.0f;

	/** Multiplier for forces from Player sources.
	 *  0.0 = ignore Player forces, 1.0 = full effect, >1.0 = amplified, <0.0 = inverted
	 *  NOT clamped - allows negative values and values > 1.0 for gameplay flexibility */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Force Filtering")
	float PlayerForceMultiplier = 1.0f;

	/** Multiplier for forces from Projectile sources.
	 *  0.0 = ignore Projectile forces, 1.0 = full effect, >1.0 = amplified, <0.0 = inverted
	 *  NOT clamped - allows negative values and values > 1.0 for gameplay flexibility */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Force Filtering")
	float ProjectileForceMultiplier = 1.0f;

	/** Multiplier for forces from Environment/World sources.
	 *  0.0 = ignore Environment forces, 1.0 = full effect, >1.0 = amplified, <0.0 = inverted
	 *  NOT clamped - allows negative values and values > 1.0 for gameplay flexibility */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Force Filtering")
	float EnvironmentForceMultiplier = 1.0f;

	/** Multiplier for forces from Physics Prop sources.
	 *  0.0 = ignore PhysicsProp forces, 1.0 = full effect, >1.0 = amplified, <0.0 = inverted
	 *  NOT clamped - allows negative values and values > 1.0 for gameplay flexibility */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Force Filtering")
	float PhysicsPropForceMultiplier = 1.0f;

	/** Multiplier for forces from sources with unknown/unspecified owner type.
	 *  0.0 = ignore unknown forces, 1.0 = full effect
	 *  NOT clamped - allows negative values and values > 1.0 for gameplay flexibility */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Force Filtering")
	float UnknownForceMultiplier = 1.0f;

	// ==================== Launched Force Filtering ====================
	// Second set of multipliers, active when NPC is in reverse-capture flight (bIsLaunched)

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Launched Force Filtering")
	float LaunchedNPCForceMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Launched Force Filtering")
	float LaunchedPlayerForceMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Launched Force Filtering")
	float LaunchedProjectileForceMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Launched Force Filtering")
	float LaunchedEnvironmentForceMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Launched Force Filtering")
	float LaunchedPhysicsPropForceMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Launched Force Filtering")
	float LaunchedUnknownForceMultiplier = 1.0f;

	/** Activate/deactivate launched force filtering (called by owning NPC) */
	UFUNCTION(BlueprintCallable, Category = "EMF|Launched Force Filtering")
	void SetLaunchedForceFilteringActive(bool bActive) { bUseLaunchedForceFiltering = bActive; }

	/** Is launched force filtering currently active? */
	UFUNCTION(BlueprintPure, Category = "EMF|Launched Force Filtering")
	bool IsLaunchedForceFilteringActive() const { return bUseLaunchedForceFiltering; }

	/** Enable minimum distance cutoff for opposite-charge sources.
	 *  When enabled, sources closer than OppositeChargeMinDistance with opposite charge sign
	 *  are ignored, preventing extreme forces from Coulomb 1/r² singularity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Force Filtering")
	bool bEnableOppositeChargeDistanceCutoff = true;

	/** Minimum distance (cm) for opposite-charge force cutoff. Sources with opposite charge
	 *  closer than this are ignored entirely. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Force Filtering", meta = (ClampMin = "1.0", Units = "cm", EditCondition = "bEnableOppositeChargeDistanceCutoff"))
	float OppositeChargeMinDistance = 35.0f;

	// ==================== Debug ====================

	/** Рисовать debug стрелки для сил и полей */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Debug")
	bool bDrawDebug = false;

	/** Логировать силы в консоль */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Debug")
	bool bLogForces = false;

	// ==================== Capture (Hard Hold) ====================

	/** Enable capture by channeling plate.
	 *  When captured, NPC is pulled toward plate and held rigidly in place. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Capture")
	bool bEnableViscousCapture = false;

	/** Constant pull-in speed (cm/s). NPC always approaches at this speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Capture", meta = (ClampMin = "100.0", ClampMax = "10000.0", Units = "cm/s", EditCondition = "bEnableViscousCapture"))
	float CaptureBaseSpeed = 1500.0f;

	/** Base capture range (cm). Actual range scales with charge:
	 *  Range = BaseRange * max(1, 1 + ln(|q_player| * |q_npc| / NormCoeff)). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Capture", meta = (ClampMin = "50.0", Units = "cm", EditCondition = "bEnableViscousCapture"))
	float CaptureBaseRange = 500.0f;

	/** Charge normalization coefficient for capture range formula.
	 *  Lower = longer range at low charges, higher = needs more charge for same range. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Capture", meta = (ClampMin = "1.0", ClampMax = "1000.0", EditCondition = "bEnableViscousCapture"))
	float CaptureChargeNormCoeff = 50.0f;

	/** Distance (cm) at which NPC snaps to plate and enters hard hold.
	 *  Below this, NPC position is locked to plate each frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Capture", meta = (ClampMin = "5.0", ClampMax = "200.0", Units = "cm", EditCondition = "bEnableViscousCapture"))
	float CaptureSnapDistance = 50.0f;

	/** Get current effective capture range (accounts for charges).
	 *  This is the dynamic range — NPCs beyond this distance auto-release. */
	UFUNCTION(BlueprintPure, Category = "EMF|Capture")
	float GetEffectiveCaptureRange() const;

	/** Mark this NPC as captured by the given plate. Enters knockback + plays montage. */
	UFUNCTION(BlueprintCallable, Category = "EMF|Capture")
	void SetCapturedByPlate(AEMFChannelingPlateActor* Plate);

	/** Release this NPC from capture. Exits knockback, stops montage. */
	UFUNCTION(BlueprintCallable, Category = "EMF|Capture")
	void ReleasedFromCapture();

	/** Is this NPC currently captured by a plate? */
	UFUNCTION(BlueprintPure, Category = "EMF|Capture")
	bool IsCapturedByPlate() const { return CapturingPlate.IsValid(); }

	/** Detach from plate without exiting captured state (for plate swap during reverse channeling) */
	void DetachFromPlate();

	/** Time (seconds) NPC must be outside CaptureRadius before auto-releasing */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Capture", meta = (ClampMin = "0.1", ClampMax = "5.0", EditCondition = "bEnableViscousCapture"))
	float CaptureReleaseTimeout = 0.5f;

	/** Reverse launch distance = CaptureRange * this multiplier */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Capture", meta = (ClampMin = "0.5", ClampMax = "5.0", EditCondition = "bEnableViscousCapture"))
	float ReverseLaunchDistanceMultiplier = 1.5f;

	/** Time (seconds) of constant-speed flight during reverse capture. Speed = Distance / Duration. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Capture", meta = (ClampMin = "0.1", ClampMax = "2.0", EditCondition = "bEnableViscousCapture"))
	float ReverseLaunchFlightDuration = 0.5f;

	/** How fast the target converges onto the camera aim line (1/s).
	 *  Higher = faster convergence. At 15, ~95% correction in 0.2s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Capture", meta = (ClampMin = "1.0", ClampMax = "50.0", EditCondition = "bEnableViscousCapture"))
	float ReverseLaunchConvergenceRate = 15.0f;

	// ==================== Events ====================

	/** Делегат: вызывается при изменении заряда */
	UPROPERTY(BlueprintAssignable, Category = "EMF|Events")
	FOnChargeChanged OnChargeChanged;

	/** Делегат: вызывается при нейтрализации заряда */
	UPROPERTY(BlueprintAssignable, Category = "EMF|Events")
	FOnChargeNeutralized OnChargeNeutralized;

	// ==================== Runtime State (ReadOnly) ====================

	/** Текущая EM сила, действующая на персонажа */
	UPROPERTY(BlueprintReadOnly, Category = "EMF|State")
	FVector CurrentEMForce = FVector::ZeroVector;

	/** Текущее ускорение от EM силы */
	UPROPERTY(BlueprintReadOnly, Category = "EMF|State")
	FVector CurrentAcceleration = FVector::ZeroVector;

	// ==================== IVelocityModifier Interface ====================

	virtual bool ModifyVelocity_Implementation(float DeltaTime, const FVector& CurrentVelocity, FVector& OutVelocityDelta) override;
	virtual float GetAccelerationMultiplier_Implementation() override;
	virtual FVector GetExternalForce_Implementation() override;

	// ==================== Public Interface ====================

	/** Получить текущий заряд (из UEMF_FieldComponent) */
	UFUNCTION(BlueprintPure, Category = "EMF")
	float GetCharge() const;

	/** Установить заряд (в UEMF_FieldComponent) */
	UFUNCTION(BlueprintCallable, Category = "EMF")
	void SetCharge(float NewCharge);

	/** Получить массу (из UEMF_FieldComponent) */
	UFUNCTION(BlueprintPure, Category = "EMF")
	float GetMass() const;

	/** Установить массу (в UEMF_FieldComponent) */
	UFUNCTION(BlueprintCallable, Category = "EMF")
	void SetMass(float NewMass);

	/** Включить/выключить EM эффекты */
	UFUNCTION(BlueprintCallable, Category = "EMF")
	void SetEnabled(bool bNewEnabled);

	/** Получить отношение заряд/масса */
	UFUNCTION(BlueprintPure, Category = "EMF")
	float GetChargeMassRatio() const;

	/** Добавить импульс от внешнего EM источника */
	UFUNCTION(BlueprintCallable, Category = "EMF")
	void AddEMImpulse(FVector Impulse);

	/** Переключить знак заряда на противоположный */
	UFUNCTION(BlueprintCallable, Category = "EMF")
	void ToggleChargeSign();

	/** Получить текущий знак заряда: 1 (положительный) или -1 (отрицательный) */
	UFUNCTION(BlueprintPure, Category = "EMF")
	int32 GetChargeSign() const;

	/** Нейтрализовать заряд (обнулить) */
	UFUNCTION(BlueprintCallable, Category = "EMF")
	void NeutralizeCharge();

	/** Получить ссылку на EMF_FieldComponent */
	UFUNCTION(BlueprintPure, Category = "EMF")
	UEMF_FieldComponent* GetFieldComponent() const { return FieldComponent; }

	/** Set the owner type of this entity's EM source (Player, NPC, Projectile, etc.)
	 *  This determines how other entities filter forces from this source */
	UFUNCTION(BlueprintCallable, Category = "EMF|Source")
	void SetOwnerType(EEMSourceOwnerType NewOwnerType);

	/** Get the owner type of this entity's EM source */
	UFUNCTION(BlueprintPure, Category = "EMF|Source")
	EEMSourceOwnerType GetOwnerType() const;

	// ==================== Charge Accumulation API ====================

	/** Добавить заряд (единый пул, без убывания) */
	UFUNCTION(BlueprintCallable, Category = "EMF|Charge")
	void AddBonusCharge(float Amount);

	/** Добавить заряд (единый пул) */
	UFUNCTION(BlueprintCallable, Category = "EMF|Charge")
	void AddPermanentCharge(float Amount);

	/** Получить бонусный заряд (legacy, всегда 0 — единый пул) */
	UFUNCTION(BlueprintPure, Category = "EMF|Charge")
	float GetBonusCharge() const { return 0.0f; }

	/** Получить заряд (со знаком полярности) */
	UFUNCTION(BlueprintPure, Category = "EMF|Charge")
	float GetBaseCharge() const { return BaseCharge; }

	/** Установить заряд (извлекает знак в ChargeSign) */
	UFUNCTION(BlueprintCallable, Category = "EMF|Charge")
	void SetBaseCharge(float NewBaseCharge);

	/** Получить итоговый заряд (ChargeSign * |BaseCharge|) */
	UFUNCTION(BlueprintPure, Category = "EMF|Charge")
	float GetTotalCharge() const;

	/** Вычесть заряд (из единого пула) */
	UFUNCTION(BlueprintCallable, Category = "EMF|Charge")
	void DeductCharge(float Amount);

	// ==================== Channeling Proxy Mode ====================

	/** Enable/disable proxy mode. In proxy mode, forces are calculated at the plate's
	 *  position from Environment sources, then applied to the player character. */
	UFUNCTION(BlueprintCallable, Category = "EMF|Channeling")
	void SetChannelingProxyMode(bool bEnable, AEMFChannelingPlateActor* PlateActor = nullptr);

	/** Is proxy mode currently active? */
	UFUNCTION(BlueprintPure, Category = "EMF|Channeling")
	bool IsInChannelingProxyMode() const { return bChannelingProxyMode; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	UPROPERTY()
	TObjectPtr<UApexMovementComponent> MovementComponent;

	UPROPERTY()
	TObjectPtr<UEMF_FieldComponent> FieldComponent;

	/** Накопленный импульс для применения в следующем кадре */
	FVector PendingImpulse = FVector::ZeroVector;

	/** Время последней нейтрализации */
	float LastNeutralizationTime = -100.0f;

	/** Предыдущий заряд (для отслеживания изменений) */
	float PreviousCharge = 0.0f;

	/** Текущий бонусный заряд (убывает со временем) */
	float CurrentBonusCharge = 0.0f;

	/** Обновить итоговый заряд в FieldComponent */
	void UpdateFieldComponentCharge();

	/** Проверить, прошёл ли cooldown после нейтрализации */
	bool CanBeNeutralized() const;

	/** Вычислить velocity delta на основе данных из FieldComponent */
	FVector ComputeVelocityDelta(float DeltaTime, const FVector& CurrentVelocity);

	/** Get force multiplier for a given source owner type (uses launched set if active) */
	float GetForceMultiplierForOwnerType(EEMSourceOwnerType OwnerType) const;

	/** True when launched force filtering multipliers should be used */
	bool bUseLaunchedForceFiltering = false;

	/** Debug визуализация */
	void DrawDebugForces(const FVector& Position, const FVector& Force) const;

	/** Check if source has effectively zero charge/current/field strength
	 *  Handles different source types: PointCharge, LineCharge, CurrentWire, etc.
	 *  @return true if source produces no force (zero charge/current/field) */
	static bool IsSourceEffectivelyZero(const FEMSourceDescription& Source);

	/** Get effective charge sign of source (+1, -1, or 0 for magnetic/neutral) */
	static int32 GetSourceEffectiveChargeSign(const FEMSourceDescription& Source);

	// ==================== Capture State ====================

	/** Plate that captured this NPC (set via SetCapturedByPlate) */
	UPROPERTY()
	TWeakObjectPtr<AEMFChannelingPlateActor> CapturingPlate;

	/** True when NPC reached plate and is rigidly held */
	bool bHardHoldActive = false;

	/** Accumulated time that NPC has been outside CaptureRadius */
	float WeakCaptureTimer = 0.0f;

	/** Reverse launch: initialized on first reverse tick */
	bool bReverseLaunchInitialized = false;

	/** Reverse launch: constant speed for the flight (locked on first frame) */
	float ReverseLaunchSpeed = 0.0f;

	/** Calculate effective capture range based on player and NPC charges.
	 *  Formula: BaseRange * max(1, 1 + ln(|q_player * q_npc| / NormCoeff)) */
	float CalculateCaptureRange() const;

	/** Apply hard-hold capture logic: pull-in or rigid hold. Returns velocity delta to apply. */
	FVector ComputeHardHoldDelta(float DeltaTime, const FVector& CurrentVelocity, AEMFChannelingPlateActor* Plate);

	// ==================== Channeling Proxy State ====================

	/** When true, forces are computed at plate position from Environment sources */
	bool bChannelingProxyMode = false;

	/** Reference to the active channeling plate actor */
	UPROPERTY()
	TWeakObjectPtr<AEMFChannelingPlateActor> ProxyPlateActor;

	/** Compute velocity delta in proxy mode (plate-to-player force relay) */
	FVector ComputeProxyVelocityDelta(float DeltaTime, const FVector& CurrentVelocity);

	/** Обработчик overlap события */
	UFUNCTION()
	void OnOwnerBeginOverlap(AActor* OverlappedActor, AActor* OtherActor);

	/** Проверить изменение заряда и вызвать делегат */
	void CheckChargeChanged();
};