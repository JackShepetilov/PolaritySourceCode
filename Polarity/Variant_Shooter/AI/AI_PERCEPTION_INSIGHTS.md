# AI Perception & StateTree: Важные инсайты

Документ с ключевыми выводами из отладки AI Perception системы для MeleeNPC.

---

## 1. Проблема: Race Condition между AIPerception и StateTree

### Симптомы
- NPC обнаруживают других NPC, но НЕ обнаруживают игрока
- Игрок обнаруживается только если был в поле зрения на BeginPlay
- Логи показывают: `PerceptionUpdated called for BP_ShooterCharacter_C_0` но цель не устанавливается

### Корневая причина
**Делегаты биндятся ПОСЛЕ того как AIPerception уже обнаружил актёров.**

Когда NPC спавнится:
1. AIPerception сразу обнаруживает все видимые акторы → вызывает `PerceptionUpdated`
2. Только потом StateTree входит в состояние `SenseEnemies`
3. Только потом биндятся делегаты `OnShooterPerceptionUpdated`
4. Игрок уже был обнаружен, повторный `PerceptionUpdated` не вызывается!

### Решение
После биндинга делегатов в `EnterState()` нужно **вручную проверить уже известные акторы**:

```cpp
// Bind delegates
InstanceData.Controller->OnShooterPerceptionUpdated.BindLambda(...);

// ВАЖНО: Проверить already-known actors
if (UAIPerceptionComponent* PerceptionComp = InstanceData.Controller->GetPerceptionComponent())
{
    TArray<AActor*> KnownActors;
    PerceptionComp->GetKnownPerceivedActors(nullptr, KnownActors);

    for (AActor* KnownActor : KnownActors)
    {
        // Обработать уже известных акторов той же логикой что и в делегате
        if (KnownActor->ActorHasTag(InstanceData.SenseTag))
        {
            // LineTrace, SetTarget и т.д.
        }
    }
}
```

**Вывод:** Делегаты - это не polling! Если событие произошло ДО биндинга, вы его пропустили.

---

## 2. Проблема: WeakContext становится невалидным

### Симптомы
- `PerceptionUpdated` вызывается для игрока ✅
- `Controller->SetCurrentTarget()` работает ✅
- Но `InstanceData.TargetActor` остаётся nullptr ❌
- StateTree не переходит в Chase состояние ❌

### Корневая причина
**WeakContext становится невалидным при асинхронных вызовах.**

```cpp
// В EnterState
OnShooterPerceptionUpdated.BindLambda(
    [WeakContext = Context.MakeWeakExecutionContext()](AActor* Actor, ...)
    {
        // AIPerception вызывает это АСИНХРОННО, возможно после transition
        FStateTreeStrongExecutionContext StrongContext = WeakContext.MakeStrongExecutionContext();

        // StrongContext.IsValid() = FALSE!
        // StateTree уже в другом состоянии или во время evaluation
        FInstanceDataType* InstanceData = StrongContext.GetInstanceDataPtr(); // = nullptr!
    }
);
```

**Почему это происходит:**
- AIPerception вызывает делегаты НЕ сразу, а через несколько кадров
- StateTree может сделать transitions, evaluations между EnterState и callback
- WeakContext привязан к конкретному выполнению Task
- Если Task "завершился" или StateTree вышел из состояния → WeakContext невалиден

### Попытка решения #1: Указатели на InstanceData ❌ КРАШ

```cpp
// ПЛОХО - приводит к dangling pointers!
AActor** TargetActorPtr = &InstanceData.TargetActor;

OnPerceptionUpdated([TargetActorPtr](...) {
    (*TargetActorPtr) = Actor;  // 💥 CRASH: EXCEPTION_ACCESS_VIOLATION
});
```

**Почему крашится:**
- InstanceData уничтожается когда StateTree выходит из состояния
- Указатель `TargetActorPtr` указывает в невалидную память
- Access Violation при попытке записи

**Аналогия:** Записал адрес квартиры друга → друг съехал → пришёл по старому адресу → там мусор или чужие люди.

### Решение #2: Двухступенчатый подход ✅

**Controller как промежуточное хранилище + Tick() синхронизация:**

```cpp
// 1. В лямбде: обновляем только Controller (он всегда живой)
OnPerceptionUpdated([Controller](...) {
    Controller->SetCurrentTarget(Actor);  // ✅ Работает всегда

    // InstanceData НЕ трогаем - WeakContext может быть невалиден!
});

// 2. В Tick(): синхронизируем Controller → InstanceData
EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime)
{
    FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

    // Копируем из Controller в InstanceData
    AActor* ControllerTarget = InstanceData.Controller->GetCurrentTarget();
    if (ControllerTarget != InstanceData.TargetActor)
    {
        InstanceData.TargetActor = ControllerTarget;
        InstanceData.bHasTarget = IsValid(ControllerTarget);
    }

    return EStateTreeRunStatus::Running;
}
```

**Почему это работает:**
- Controller существует всё время пока NPC жив
- InstanceData доступен в Tick() через `Context.GetInstanceData()` (синхронный вызов)
- Tick() вызывается StateTree'ом, контекст гарантированно валиден
- Производительность: ~10-20 наносекунд на NPC (сравнение + присвоение указателей)

---

## 3. Понятие указателей и dangling pointers

### Что такое указатель
**Указатель** = адрес в памяти, "закладка на странице в блокноте"

```cpp
int age = 25;        // Переменная хранит значение 25 (на "странице 100")
int* ptr = &age;     // Указатель хранит адрес: "данные на странице 100"
*ptr = 30;           // Записать 30 по адресу "страница 100"
```

### Dangling pointer (висячий указатель)

**Аналогия с квартирой:**
1. Друг живёт по адресу "ул. Ленина, 10, кв. 5"
2. Ты записал адрес в телефон
3. Друг съехал
4. В квартиру въехал кто-то другой (или она пустая)
5. Ты приходишь по старому адресу → чужие люди или пустота!

**В коде:**
```cpp
AActor** TargetActorPtr = &InstanceData.TargetActor;  // Сохранили адрес "страница 500"

// StateTree выходит из состояния → InstanceData уничтожается
// "Страница 500" теперь содержит мусор!

(*TargetActorPtr) = NewActor;  // 💥 Пытаемся писать в мусор → CRASH
```

### WeakContext vs StrongContext

**WeakContext** = "Умный адрес с проверкой: живёт ли там ещё друг?"

**StrongContext** = Результат проверки:
- Если друг живёт → валидный доступ к данным
- Если съехал → nullptr

```cpp
WeakContext = Context.MakeWeakExecutionContext();  // Сохранили "умную ссылку"

// Позже, в callback:
StrongContext = WeakContext.MakeStrongExecutionContext();  // Проверили

if (StrongContext.IsValid()) {  // Друг ещё там?
    InstanceData* data = StrongContext.GetInstanceDataPtr();  // Получаем доступ
    data->TargetActor = Actor;  // ✅ Безопасно
} else {
    // Друг съехал - не лезем в чужую квартиру!
}
```

---

## 4. Производительность Tick-based синхронизации

### Что происходит каждый кадр:
```cpp
AActor* ControllerTarget = InstanceData.Controller->GetCurrentTarget();  // ~0 нс
if (ControllerTarget != InstanceData.TargetActor)  // ~0 нс (сравнение указателей)
{
    InstanceData.TargetActor = ControllerTarget;  // ~0 нс (присвоение)
    InstanceData.bHasTarget = IsValid(ControllerTarget);  // ~5-10 нс
}
```

**Итого: ~10-20 наносекунд на одного NPC**

### Масштабируемость:
- 100 NPC × 60 FPS × 20 нс = **0.12 мс/сек**
- Это **0.2%** от frame budget (16.6ms @ 60fps)

### Почему приемлемо:
- ✅ Тривиальные операции (чтение/сравнение указателей)
- ✅ Cache-friendly (локальность данных)
- ✅ Выполняется только в SenseEnemies состоянии
- ✅ Branch prediction работает идеально (цель редко меняется)
- ✅ Простой и читаемый код

**Что РЕАЛЬНО стоит CPU:** AIPerception raytracing, NavMesh pathfinding, Physics, Animation.

**Вывод:** Преждевременная оптимизация - корень зла. Это работает, это просто, это быстро.

---

## 5. Альтернативные подходы (не использованные)

### FStateTreeDelegateDispatcher (официальный способ)
- Предназначен для **editor-bound delegates**, а не runtime callbacks
- Требует настройки в editor, не подходит для динамических событий AIPerception
- [Документация](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/StateTreeModule/FStateTreeDelegateDispatcher)

### SendEvent вместо прямого обновления InstanceData
- Более "правильный" архитектурно подход
- StateTree реагирует на события через transitions
- Но сложнее в реализации, больше кода
- Производительность аналогична Tick-based подходу

### UObject InstanceData для dynamic delegates
- Позволяет биндить dynamic delegates (требуют UObject)
- Epic Games: "not very well tested, possibly dangerous"
- [Источник](https://zomgmoz.tv/unreal/State-Tree/StateTree-InstanceData)

---

## 6. Ключевые выводы

### ✅ DO:
1. **Проверяй already-known actors** после биндинга делегатов
2. **Используй Controller как промежуточное хранилище** для async данных
3. **Синхронизируй через Tick()** если WeakContext ненадёжен
4. **Проверяй StrongContext.IsValid()** перед доступом к InstanceData

### ❌ DON'T:
1. **НЕ сохраняй сырые указатели** на поля InstanceData в лямбдах
2. **НЕ полагайся** что WeakContext будет валиден в async callbacks
3. **НЕ делай преждевременную оптимизацию** без профайлера
4. **НЕ используй UObject InstanceData** без крайней необходимости

### 🧠 Концептуальное понимание:
- **Делегаты** - это callbacks, не polling
- **WeakContext** - валиден только в синхронном контексте StateTree
- **InstanceData** - живёт только пока Task в активном состоянии
- **Controller** - живёт всё время пока NPC существует
- **Tick()** - гарантированно синхронный контекст с валидным InstanceData

---

## 7. Полезные ссылки

- [FStateTreeDelegateDispatcher API](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/StateTreeModule/FStateTreeDelegateDispatcher)
- [StateTree InstanceData Best Practices](https://zomgmoz.tv/unreal/State-Tree/StateTree-InstanceData)
- [Custom StateTree Tasks Guide](https://zomgmoz.tv/unreal/State-Tree/Custom-StateTree-tasks)
- [FStateTreeExecutionContext API](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/StateTreeModule/FStateTreeExecutionContext)
- [StateTree Async Support Roadmap](https://portal.productboard.com/epicgames/1-unreal-engine-public-roadmap/c/2028-statetree-delegates-and-asynchronous-task-support)

---

**Автор:** Claude Sonnet 4.5
**Дата:** 2026-01-25
**Контекст:** Отладка AI Perception для MeleeNPC в проекте Polarity
