// OutlineComponent.h
// Компонент для подсветки объектов через Custom Stencil + Post Process

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "OutlineComponent.generated.h"

/**
 * Тип подсветки объекта
 * Stencil Values: Destroy=1, Enemy=2, Charge=3, Interact=4
 */
UENUM(BlueprintType)
enum class EOutlineType : uint8
{
	None = 0        UMETA(DisplayName = "None"),
	Destroy = 1     UMETA(DisplayName = "Destroy (Through Walls)"),
	Enemy = 2       UMETA(DisplayName = "Enemy"),
	Charge = 3      UMETA(DisplayName = "Charge"),
	Interact = 4    UMETA(DisplayName = "Interact (Through Walls)")
};

/**
 * Компонент подсветки объектов
 * Добавляется к актору и автоматически настраивает Custom Stencil на всех мешах
 * 
 * Использование:
 * 1. Добавить компонент к актору
 * 2. Выбрать OutlineType
 * 3. Post Process Material читает Stencil и рисует outline
 */
UCLASS(ClassGroup=(Rendering), meta=(BlueprintSpawnableComponent))
class POLARITY_API UOutlineComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UOutlineComponent();

	// Тип подсветки
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outline")
	EOutlineType OutlineType = EOutlineType::None;

	// Включена ли подсветка
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outline")
	bool bOutlineEnabled = true;

	// Включить подсветку
	UFUNCTION(BlueprintCallable, Category = "Outline")
	void EnableOutline();

	// Выключить подсветку
	UFUNCTION(BlueprintCallable, Category = "Outline")
	void DisableOutline();

	// Сменить тип подсветки
	UFUNCTION(BlueprintCallable, Category = "Outline")
	void SetOutlineType(EOutlineType NewType);

	// Проверка: видна ли подсветка сквозь стены
	UFUNCTION(BlueprintPure, Category = "Outline")
	bool IsVisibleThroughWalls() const;

	// Получить Stencil значение для текущего типа
	UFUNCTION(BlueprintPure, Category = "Outline")
	int32 GetStencilValue() const;

protected:
	virtual void BeginPlay() override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	// Применить настройки Stencil ко всем мешам актора
	void ApplyStencilToMeshes();

	// Убрать Stencil со всех мешей
	void RemoveStencilFromMeshes();

	// Кэш компонентов для быстрого доступа
	UPROPERTY()
	TArray<TWeakObjectPtr<UPrimitiveComponent>> CachedMeshComponents;

	// Обновить кэш мешей
	void UpdateMeshCache();
};
