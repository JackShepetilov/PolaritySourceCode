# Гайд по созданию апгрейдов

Шаблон создания нового апгрейда с поддержкой **многоуровневой прокачки** и **per-level настройки через DataAsset**. Reference-имплементация — `Upgrade_AirDash` + `UpgradeDefinition_AirDash`.

## Архитектура (TL;DR)

| Слой | Класс | Где живёт | Назначение |
|---|---|---|---|
| Данные | `UUpgradeDefinition` (base) + `UUpgradeDefinition_X` (subclass) | DataAsset в Content | Метаданные (имя/описание/иконка) + per-level tuning |
| Реестр | `UUpgradeRegistry.AllUpgrades` | DataAsset | Каталог всех доступных апгрейдов в игре |
| Runtime | `UUpgradeComponent` (base) + `UUpgrade_X` (subclass) | Дин. компонент на `ShooterCharacter` | Игровая логика апгрейда |
| Менеджер | `UUpgradeManagerComponent` | На `ShooterCharacter` | Грантит/удаляет апгрейды, держит уровни |
| UI | `UUpgradeChoiceWidget` + `UUpgradeCardWidget` | HUD | Модал выбора на level-up |
| XP | `UXPSubsystem` (категория `ESkillCategory`) | GameInstance | Триггерит level-up → choice |

**Ключевое правило:** на repeat-grant `UUpgradeManagerComponent::GrantUpgrade` повышает `CurrentLevel` и зовёт `OnLevelChanged(Old, New)`. На первом гранте — `OnUpgradeActivated` (CurrentLevel уже = 1).

---

## Шаги для нового апгрейда

### 1. Создай `UpgradeDefinition_X.h/.cpp`

Наследник `UUpgradeDefinition` с per-level tuning struct + массивом + helper'ом.

```cpp
// UpgradeDefinition_X.h
#pragma once
#include "CoreMinimal.h"
#include "UpgradeDefinition.h"
#include "UpgradeDefinition_X.generated.h"

USTRUCT(BlueprintType)
struct FXLevelData
{
    GENERATED_BODY()

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "X", meta = (ClampMin = "0"))
    float SomeParam = 1.0f;
    // ... другие per-level параметры
};

UCLASS(BlueprintType)
class POLARITY_API UUpgradeDefinition_X : public UUpgradeDefinition
{
    GENERATED_BODY()
public:
    /** Per-level tuning. Index 0 = Lv 1, index 1 = Lv 2. Length = MaxLevel (auto-synced). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "X")
    TArray<FXLevelData> LevelData;

    UFUNCTION(BlueprintPure, Category = "X")
    const FXLevelData& GetLevelData(int32 Level) const;

#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
```

```cpp
// UpgradeDefinition_X.cpp
#include "UpgradeDefinition_X.h"

const FXLevelData& UUpgradeDefinition_X::GetLevelData(int32 Level) const
{
    static const FXLevelData FallbackEmpty;
    if (LevelData.Num() == 0) return FallbackEmpty;
    const int32 Idx = FMath::Clamp(Level - 1, 0, LevelData.Num() - 1);
    return LevelData[Idx];
}

#if WITH_EDITOR
void UUpgradeDefinition_X::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);
    const int32 NewMaxLevel = FMath::Max(1, LevelData.Num());
    if (MaxLevel != NewMaxLevel) MaxLevel = NewMaxLevel;
}
#endif
```

### 2. Создай `Upgrade_X.h/.cpp`

Наследник `UUpgradeComponent` с тремя хуками.

```cpp
// Upgrade_X.h
#pragma once
#include "CoreMinimal.h"
#include "UpgradeComponent.h"
#include "Upgrade_X.generated.h"

UCLASS(BlueprintType, meta = (DisplayName = "X"))
class POLARITY_API UUpgrade_X : public UUpgradeComponent
{
    GENERATED_BODY()
public:
    UUpgrade_X();
protected:
    virtual void OnUpgradeActivated() override;
    virtual void OnUpgradeDeactivated() override;
    virtual void OnLevelChanged(int32 OldLevel, int32 NewLevel) override;
private:
    void ApplyForLevel(int32 Level);

    // Если апгрейд модифицирует shared-asset (MovementSettings, MeleeAttackSettings и т.п.):
    void CaptureBaseline();
    void RevertToBaseline();
    /* ... cached baseline fields ... */
    bool bBaselineCaptured = false;
};
```

```cpp
// Upgrade_X.cpp
#include "Upgrade_X.h"
#include "UpgradeDefinition_X.h"
#include "ShooterCharacter.h"
// ... + конкретные includes для модифицируемых компонентов

void UUpgrade_X::OnUpgradeActivated()
{
    // Базовая активация (включить флаг, забиндить делегат и т.п.)
    CaptureBaseline();        // если модифицируем shared asset
    ApplyForLevel(CurrentLevel);
}

void UUpgrade_X::OnUpgradeDeactivated()
{
    // Откатить активацию
    RevertToBaseline();       // если был capture
}

void UUpgrade_X::OnLevelChanged(int32 OldLevel, int32 NewLevel)
{
    ApplyForLevel(NewLevel);
}

void UUpgrade_X::ApplyForLevel(int32 Level)
{
    const UUpgradeDefinition_X* Def = Cast<UUpgradeDefinition_X>(UpgradeDefinition);
    if (!Def) return;
    const FXLevelData& Data = Def->GetLevelData(Level);

    // Применить Data.* к компонентам персонажа
}
```

### 3. В редакторе

1. Создай `DA_X` Data Asset → класс `UpgradeDefinition_X`.
2. Заполни базовые поля: `UpgradeTag` (уникальный gameplay tag), `Category` (Movement/Melee/EMF/Weapon), `DisplayName`, `Description`, `Icon`, `Tier`, `ComponentClass = Upgrade_X`.
3. Заполни `LevelData` — по одной entry на уровень. `MaxLevel` подставится автоматически.
4. **Добавь `DA_X` в `UpgradeRegistry.AllUpgrades`** — без этого апгрейд не появится в choice-pool.

---

## Reference-имплементация: AirDash

См. файлы `UpgradeDefinition_AirDash.h/.cpp` и `Upgrade_AirDash.h/.cpp`.

- `FAirDashLevelData`: `MaxCharges`, `CooldownSeconds`, `ImpulseMultiplier`.
- `Upgrade_AirDash` модифицирует `ApexMovementComponent->MovementSettings` (shared DataAsset!), поэтому используется паттерн **CaptureBaseline + RevertToBaseline**.
- Дополнительно: при level-up синхронизируется `RemainingAirDashCount`, чтобы новые charges работали сразу, без приземления.

---

## Правила и подводные камни

### MaxLevel = LevelData.Num()
`PostEditChangeProperty` автоматически синхронизирует `MaxLevel`. Никогда не редактируй `MaxLevel` руками — установится сам.

### `OnUpgradeActivated` vs `OnLevelChanged`
- **OnUpgradeActivated** зовётся **только на первом гранте**, когда `CurrentLevel = 1`. Используй для one-time setup (биндинги делегатов, флаги).
- **OnLevelChanged(Old, New)** зовётся **только на repeat-grants** (Lv 1→2, 2→3, ...), не на первом.
- В большинстве случаев в `OnUpgradeActivated` зови `ApplyForLevel(CurrentLevel)` после base-setup, чтобы не дублировать код применения.

### Shared assets (CRITICAL)
`MovementSettings`, `MeleeAttackSettings` и подобные — это **shared DataAsset**'ы, один экземпляр на проект. Если апгрейд их модифицирует:
- В `OnUpgradeActivated` сделай `CaptureBaseline()` — снимок исходных значений в private fields апгрейда.
- В `OnUpgradeDeactivated` сделай `RevertToBaseline()` — иначе при выходе в меню значения останутся искажёнными (в PIE — до перезапуска редактора, в shipping build — до нового запуска игры).

В singleplayer roguelite это OK. В coop с несколькими Character'ами — каждый персонаж будет ломать настройки другого; тогда нужна **per-actor копия Settings**, не модификация shared.

### Filter в Choice
`UUpgradeChoiceWidget::RollChoicesForCategory` исключает из пула те апгрейды, у которых `IsUpgradeMaxedOut = true` (т.е. `CurrentLevel >= MaxLevel`). Промежуточные уровни остаются в пуле для повышения.

### Event на UI карточке
`UUpgradeCardWidget::BP_OnInitialized` получает 6 параметров:
```
InName, InDescription, InIcon, InTier, InCurrentLevel, InMaxLevel
```
- `InCurrentLevel == 0` — апгрейд новый (показать "NEW" или "Lv 1").
- `InCurrentLevel >= 1` — апгрейд уже есть, выбор повысит до `InCurrentLevel + 1` (показать "Lv N → N+1").
- При `InCurrentLevel + 1 == InMaxLevel` — следующий уровень последний (можно выделить "MAX NEXT").

### Save / Load
`UUpgradeManagerComponent::GetUpgradeTagsForSave / RestoreUpgradesFromTags` сейчас сохраняют **только теги**, не уровни. Если понадобится save mid-run — расширить до `TMap<FGameplayTag, int32>` (тег → уровень).

---

## Чек-лист перед коммитом нового апгрейда

- [ ] `UpgradeDefinition_X.h/.cpp` — struct + TArray + GetLevelData + PostEditChangeProperty
- [ ] `Upgrade_X.h/.cpp` — OnUpgradeActivated/Deactivated/LevelChanged + ApplyForLevel
- [ ] Если модифицируется shared asset — CaptureBaseline + RevertToBaseline
- [ ] `DA_X` создан, `UpgradeTag` уникальный, `Category` правильная, `ComponentClass = Upgrade_X`
- [ ] `LevelData` заполнен N entries (= MaxLevel)
- [ ] `DA_X` добавлен в `UpgradeRegistry.AllUpgrades`
- [ ] DataAsset кривых XP (`XPConfig.SkillCurves`) содержит entry для соответствующей `ESkillCategory`
