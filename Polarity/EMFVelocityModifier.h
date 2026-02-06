// EMFVelocityModifier.h
// Integrates EMF_Plugin electromagnetic forces with ApexMovementComponent

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VelocityModifier.h"
#include "EMFVelocityModifier.generated.h"

class UApexMovementComponent;
class UEMF_FieldComponent;

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

	/** Базовый заряд (постоянный, определяет знак полярности) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Charge Accumulation")
	float BaseCharge = 10.0f;

	/** Максимальный базовый (стабильный) заряд - от Melee Dummy и т.п. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Charge Accumulation", meta = (ClampMin = "0.0"))
	float MaxBaseCharge = 30.0f;

	/** Заряд, добавляемый за каждый успешный удар в ближнем бою */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Charge Accumulation", meta = (ClampMin = "0.0"))
	float ChargePerMeleeHit = 2.0f;

	/** Максимальный бонусный (нестабильный) заряд от ударов по врагам */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Charge Accumulation", meta = (ClampMin = "0.0"))
	float MaxBonusCharge = 20.0f;

	/** Скорость убывания бонусного заряда (единиц/сек) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Charge Accumulation", meta = (ClampMin = "0.0"))
	float BonusChargeDecayRate = 3.0f;

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

	/** Multiplier for forces from sources with unknown/unspecified owner type.
	 *  0.0 = ignore unknown forces, 1.0 = full effect
	 *  NOT clamped - allows negative values and values > 1.0 for gameplay flexibility */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Force Filtering")
	float UnknownForceMultiplier = 1.0f;

	// ==================== Debug ====================

	/** Рисовать debug стрелки для сил и полей */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Debug")
	bool bDrawDebug = false;

	/** Логировать силы в консоль */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Debug")
	bool bLogForces = false;

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

	/** Добавить бонусный заряд (от melee удара) - убывает со временем */
	UFUNCTION(BlueprintCallable, Category = "EMF|Charge")
	void AddBonusCharge(float Amount);

	/** Добавить постоянный заряд (не убывает) - увеличивает BaseCharge */
	UFUNCTION(BlueprintCallable, Category = "EMF|Charge")
	void AddPermanentCharge(float Amount);

	/** Получить текущий бонусный заряд */
	UFUNCTION(BlueprintPure, Category = "EMF|Charge")
	float GetBonusCharge() const { return CurrentBonusCharge; }

	/** Получить базовый заряд */
	UFUNCTION(BlueprintPure, Category = "EMF|Charge")
	float GetBaseCharge() const { return BaseCharge; }

	/** Установить базовый заряд (меняет знак полярности) */
	UFUNCTION(BlueprintCallable, Category = "EMF|Charge")
	void SetBaseCharge(float NewBaseCharge);

	/** Получить итоговый заряд (Base + Bonus) */
	UFUNCTION(BlueprintPure, Category = "EMF|Charge")
	float GetTotalCharge() const;

	/** Вычесть заряд (сначала из бонуса, потом из базы) */
	UFUNCTION(BlueprintCallable, Category = "EMF|Charge")
	void DeductCharge(float Amount);

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

	/** Get force multiplier for a given source owner type */
	float GetForceMultiplierForOwnerType(EEMSourceOwnerType OwnerType) const;

	/** Debug визуализация */
	void DrawDebugForces(const FVector& Position, const FVector& Force) const;

	/** Check if source has effectively zero charge/current/field strength
	 *  Handles different source types: PointCharge, LineCharge, CurrentWire, etc.
	 *  @return true if source produces no force (zero charge/current/field) */
	static bool IsSourceEffectivelyZero(const FEMSourceDescription& Source);

	/** Обработчик overlap события */
	UFUNCTION()
	void OnOwnerBeginOverlap(AActor* OverlappedActor, AActor* OtherActor);

	/** Проверить изменение заряда и вызвать делегат */
	void CheckChargeChanged();
};