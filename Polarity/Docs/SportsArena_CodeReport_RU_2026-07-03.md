# Sports Arena Code Report

## Scope

Отчет описывает только код и реализованный функционал из текущей сессии.

## Modified Files

- `C:\Users\Professional\Documents\Unreal Projects\Polarity_Main5_8\Source\Polarity\Arena\SportsBall.h`
- `C:\Users\Professional\Documents\Unreal Projects\Polarity_Main5_8\Source\Polarity\Arena\SportsBall.cpp`
- `C:\Users\Professional\Documents\Unreal Projects\Polarity_Main5_8\Source\Polarity\Arena\SportsGoal.h`
- `C:\Users\Professional\Documents\Unreal Projects\Polarity_Main5_8\Source\Polarity\Arena\SportsGoal.cpp`
- `C:\Users\Professional\Documents\Unreal Projects\Polarity_Main5_8\Source\Polarity\Variant_Shooter\MeleeAttackComponent.cpp`
- `C:\Users\Professional\Documents\Unreal Projects\Polarity_Main5_8\Source\Polarity\ApexMovementComponent.cpp`

## ASportsBall

Добавлен актор `ASportsBall`.

Основной функционал:

- физический мяч на `UStaticMeshComponent`;
- по умолчанию использует сферу из Engine Basic Shapes;
- `Simulate Physics = true`;
- collision profile: `PhysicsActor`;
- object type: `ECC_PhysicsBody`;
- `ECC_Pawn = Block`;
- `ECC_Visibility = Block`;
- CCD включен;
- overlap events включены для взаимодействия с `GoalVolume`;
- актор получает тег `SportsBall`.

Настраиваемые параметры физики:

- `BallDiameter`;
- `BallMassKg`;
- `LinearDamping`;
- `AngularDamping`;
- `bUseCCD`;
- `BallPhysicalMaterial`.

Обработка melee-попаданий:

- публичная функция: `HandleMeleeAttackHit(...)`;
- `Ground` считается панчем;
- панч регистрируется, но не применяет импульс;
- `Airborne` считается пинком;
- `Sliding` считается пинком;
- пинки применяют импульс через `AddImpulseAtLocation`;
- spin добавляется через `AddAngularImpulseInDegrees`.

Параметры пинка:

- `KickVelocityChange = 1400`;
- `KickUpwardBias = 0.08`;
- `PlayerVelocityToKickScale = 0.35`;
- `MaxPlayerVelocityBonus = 700`;
- `KickSpinVelocityChange = 900`;
- `SlidingKickUpVelocityChange = 650`;
- `SlidingKickForwardVelocityChange = 450`.

Подкатный удар:

- добавляет дополнительную forward-составляющую;
- гарантирует минимальную вертикальную составляющую через `SlidingKickUpVelocityChange`;
- результат: мяч подлетает, а не только катится по полу.

Контакт игрока с мячом в подкате:

- старый overlap-подход удален;
- `Tick` для контактного push удален;
- обработка идет через `OnComponentHit`;
- функция: `OnBallHit(...)`;
- если игрок скользит и движется в сторону мяча, мячу добавляется импульс;
- мяч остается blocking-объектом и не пропускает игрока сквозь себя.

Параметры body-push в подкате:

- `SlidingBodyPushMinSpeed = 150`;
- `SlidingBodyPushVelocityScale = 0.45`;
- `SlidingBodyPushMaxVelocityChange = 450`;
- `SlidingBodyPushMaxBallSpeed = 1800`.

Состояние и события:

- `PunchHitCount`;
- `KickHitCount`;
- `LastMeleeAttackType`;
- `bLastHitAppliedKick`;
- `LastAppliedImpulse`;
- `LastHitLocation`;
- `LastAttacker`;
- `OnSportsBallMeleeHit`.

## UMeleeAttackComponent

Изменен unarmed melee path для поддержки мяча.

Функционал:

- подключен `Arena/SportsBall.h`;
- `ASportsBall` считается валидной melee-целью;
- melee sweep ищет `ECC_Pawn` и `ECC_PhysicsBody`;
- результаты sweep сортируются по дистанции;
- при попадании по `ASportsBall` вызывается `HandleMeleeAttackHit(...)`;
- урон по мячу не применяется;
- обработка мяча отделена от обычного damage path.

Передаваемые в мяч данные:

- атакующий актор;
- `FHitResult`;
- `EMeleeAttackType`;
- направление trace;
- скорость атакующего.

## UApexMovementComponent

Добавлена защита от wall-bounce на мяче.

Функционал:

- акторы с тегом `SportsBall` не считаются валидной wall-run surface;
- `CheckForWallBounce()` игнорирует hit по актору с тегом `SportsBall`;
- подкат не должен отражать игрока от мяча как от стены;
- мяч при этом остается физическим blocking-объектом.

## ASportsGoal

Добавлен актор `ASportsGoal`.

Компоненты:

- `SceneRoot`;
- `GoalVolume`;
- `FrameMesh`;
- `NetMesh`;
- `ExplosionOrigin`.

Основной функционал:

- гол срабатывает при входе `ASportsBall` в `GoalVolume`;
- минимального порога скорости для гола нет;
- гол одноразовый;
- после гола мяч останавливается, скрывается и выключает collision;
- сетка скрывается и выключает collision;
- событие гола доступно через Blueprint delegate `OnSportsGoalScored`.

Событие гола передает:

- `ASportsGoal* Goal`;
- `ASportsBall* Ball`;
- `float BallSpeed`;
- `float GoalPower`;
- `FVector GoalLocation`.

## Goal Power

Сила гола считается от скорости мяча в момент попадания.

Логика:

- берется физическая скорость мяча через `GetPhysicsLinearVelocity()`;
- если задан `SpeedToPowerCurve`, используется значение кривой;
- иначе используется нормализация скорости через `FullPowerSpeed`;
- результат clamp-ится в диапазон `0..1`.

Значения по умолчанию:

- `FullPowerSpeed = 3000`;
- `SlowGoalPower = 0.12`.

`GoalPower` масштабирует:

- радиус radial impulse;
- силу radial impulse;
- масштаб VFX;
- громкость SFX;
- pitch SFX.

## Goal VFX And SFX

В `ASportsGoal` добавлены поля:

- `GoalVFX`;
- `SlowVFXScale`;
- `FullVFXScale`;
- `GoalSound`;
- `SlowSoundVolume`;
- `FullSoundVolume`;
- `SlowSoundPitch`;
- `FullSoundPitch`.

Проигрывание:

- VFX спавнится через `UNiagaraFunctionLibrary::SpawnSystemAtLocation`;
- SFX проигрывается через `UGameplayStatics::PlaySoundAtLocation`;
- scale, volume и pitch считаются через `GoalPower`.

## Geometry Collection Support

В `ASportsGoal` добавлена поддержка разрушения рамы через Geometry Collection.

Функционал:

- поле `FrameGeometryCollection`;
- при голе может спавниться `AGeometryCollectionActor`;
- `FrameMesh` скрывается;
- материалы копируются с `FrameMesh`;
- применяется cluster strain;
- применяется radial impulse;
- можно заморозить debris после задержки.

Поля:

- `FrameGeometryCollection`;
- `GCInitialDelay`;
- `GCClusterStrain`;
- `GCGibFreezeTime`;
- `GCRadialImpulseDelay`;

## Goal Impulse Settings

Поля масштабируемого импульса:

- `SlowImpulseRadius`;
- `FullImpulseRadius`;
- `SlowRadialImpulse`;
- `FullRadialImpulse`;
- `ExtraImpulsePieces`.

Функционал:

- радиус и сила считаются через `GoalPower`;
- radial impulse применяется к `FrameMesh`, если он симулирует физику;
- radial impulse применяется к `ExtraImpulsePieces`, если они заданы и симулируют физику.

## Current Verification Status

- После последних правок сборка не запускалась.
- Выполнена текстовая проверка отсутствия старого pawn-overlap пути для мяча.
- Проверено отсутствие старых символов:
  - `OverlapPawn`;
  - `SlidingContact`;
  - `OnBallBeginOverlap`;
  - `OnBallEndOverlap`;
  - старого pawn-overlap режима;
  - старого `Tick` в `ASportsBall`.
