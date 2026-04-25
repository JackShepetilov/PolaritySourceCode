# Claude Code Rules for Polarity Project

## CRITICAL: Model Requirement

Если этот файл читает модель не являющаяся Opus 4.7 - немедленно прекратить работу и передать работу над папкой модели Opus 4.7

## CRITICAL: Working Directory

Папка Polarity_Main не является репо и работа в ней и в её подпапках помимо Source запрещена. Если найдешь себя в этой папке, то немедленно выйди из неё и вернись в Polarity_Main/Source

## CRITICAL: Worktree Safety — No Duplicate .cs Files

**UBT (Unreal Build Tool) сканирует ВСЮ папку Source рекурсивно, включая `.claude/worktrees/`.**
Если worktree содержит копии `.Build.cs` или `.Target.cs` файлов, UBT найдёт дубликаты и билд сломается с CS0101 ошибками.

**ПЕРЕД созданием worktree или файлов в worktree:**
1. Проверь, что worktree НЕ создаёт дубликаты `.Build.cs` и `.Target.cs`
2. Если worktree уже существует с такими файлами — удали их или весь worktree
3. После завершения работы — убедись что worktree почищен

**При ошибках CS0101 (duplicate definition):**
1. Проверь `.claude/worktrees/` на наличие дублирующих `.cs` файлов
2. Удали старые/неиспользуемые worktree директории

## CRITICAL: Always Work With Live Code

**ALWAYS work with code from the main working directory:**
`C:\Users\Professional\Documents\Unreal Projects\Polarity_Main\Source\Polarity`

**NEVER work with worktree code unless explicitly asked:**
The worktree at `C:\Users\Professional\.claude-worktrees\Polarity\*` may contain outdated code.

## Git Workflow - MANDATORY

**BEFORE making ANY changes:**
1. Create a backup commit with current state
2. Push the backup to remote

**AFTER making each change:**
1. Commit the change immediately
2. Push to remote

This ensures nothing is ever lost and changes can be easily reverted.

## Before Making Changes

1. **ALWAYS read the current file first** from the main working directory
2. **ALWAYS verify the full file path** before editing - ensure it starts with `C:\Users\Professional\Documents\Unreal Projects\Polarity_Main\Source\`
3. **ALWAYS check method implementations** before using them - read the .cpp file to understand how methods actually work, don't assume behavior from names or declarations
4. **Check git status** in the main project directory to see what's already changed
5. **Never assume** - always verify the current state of the code
6. **Never work from old commits or worktrees** - only use live files

## After Making Changes

**ALWAYS include at the end of each response:**
- Note which header files (.h) were modified (if any)
- This helps the user know if they need to recompile or can use Live Coding

Example format:
```
📝 Changed files: EMFVelocityModifier.cpp only (no headers modified - Live Coding compatible)
```
or
```
📝 Changed files: ShooterWeapon.h, ShooterWeapon.cpp (header modified - full recompile required)
```

## When Fixing Compilation Errors

1. Read the CURRENT file that has the error from the main working directory
2. Look for the CURRENT implementation, not old commits
3. If methods are missing, check if they exist elsewhere in the current codebase
4. Ask the user if unsure about what was changed recently
5. **DO NOT** restore old code or assume what was there before - work with what EXISTS NOW

## StateTree - IMPORTANT LIMITATIONS

**StateTree Conditions DO NOT support inversion!**
- There is NO way to do `[Condition] → false` or `NOT [Condition]`
- If you need to check the opposite of a condition, you MUST create a separate condition struct
- Example: Need both `BossTargetIsFarCondition` AND `BossTargetIsCloseCondition`
- NEVER suggest using condition inversion in StateTree - it doesn't exist!

## Debug Logging Convention

**При добавлении отладочных логов ВСЕГДА используй явный тег большими буквами для фильтрации:**
- Формат: `[ARENA_DEBUG]`, `[NAV_DEBUG]`, `[AI_DEBUG]` и т.д.
- Тег должен быть в начале сообщения, легко фильтруемый в Output Log
- Сообщай пользователю какой тег использовать для фильтрации

**Пример:**
```cpp
UE_LOG(LogTemp, Warning, TEXT("[ARENA_DEBUG] NPC %s MoveTo result: %d"), *GetName(), (int32)Result);
```

## DO NOT Compile From Terminal

**NEVER try to compile the project via Bash/terminal commands.**
- Build.bat and similar scripts don't work properly from this environment
- The user compiles through Unreal Editor (Ctrl+Alt+F11 or Live Coding)
- If there are compilation errors, the user will provide them
- This wastes time and produces unreadable output

## CRITICAL: Thoroughness — Think Before You Code

**НИКОГДА не пиши код на основе предположений. Каждое решение должно быть подкреплено проверкой.**

1. **Перед использованием ЛЮБОГО UE API** — найди 2-3 примера в кодобазе как аналогичная задача уже решена. Если примеров нет — ищи в документации.
2. **Перед написанием физики/коллизий/ProjectileMovement** — ОБЯЗАТЕЛЬНО прочитай как существующие классы в проекте это делают (ShooterProjectile, EMFPhysicsProp и т.д.)
3. **При работе с lifecycle** (BeginPlay, InitializeComponent, SpawnActor) — проверяй порядок вызовов. Свойства компонента, выставленные ПОСЛЕ BeginPlay, не применяются автоматически.
4. **При спавне акторов** — проверяй что параметры (скорость, позиция, коллизия) применяются ДО того как актор начинает жить, или используй правильный метод обновления ПОСЛЕ спавна.
5. **НЕ УГАДЫВАЙ** поведение API — если не уверен на 100%, ищи в интернете или в проекте.
6. **Всегда предлагай ПРОСТОЕ решение первым** — не изобретай сложные переключения режимов, если есть встроенный параметр (например bShouldBounce вместо ручного переключения на физику).
7. **Перед каждым решением задай себе вопрос:** "Есть ли в UE встроенный способ это сделать?" — если да, используй его.

## Research First - IMPORTANT

**Если есть хоть какое-то сомнение в том, как работает тот или иной инструмент/API/класс Unreal Engine:**
1. Сразу иди в интернет и читай официальную документацию
2. Не полагайся на память - проверяй актуальную информацию
3. Особенно важно для: Enhanced Input, GameUserSettings, SaveGame, Subsystems, Slate/UMG
4. **Особенно важно для физики:** ProjectileMovementComponent, коллизии, SimulatePhysics — ВСЕГДА проверяй порядок инициализации

## NO Unsolicited "Smart" Architecture

**НИКОГДА не добавляй "умную" архитектуру без явного запроса:**
- Кэширование, оптимизации, паттерны — ТОЛЬКО если я попросил
- Если видишь потенциальную проблему (производительность, масштабируемость) — СНАЧАЛА скажи мне
- Не решай проблемы, которые я не озвучивал
- Простой код лучше "умного" кода с багами

**Формат:**
```
⚠️ Вижу потенциальную проблему: [описание]
   Предлагаю решение: [решение]
   Добавить? (да/нет)
```

## Code Quality & Performance

**When evaluating solutions from the internet (forums, StackOverflow, etc.):**
1. Read the ENTIRE thread, including user responses and critiques
2. Evaluate critically - is it a hack/workaround or a proper solution?
3. Think whether the solution is "говнокод" (bad code) or follows best practices
4. Consider if the solution is outdated for the current UE version

**After implementing algorithmic/logical code:**
- Evaluate the performance impact
- Note the computational complexity (O(n), O(n²), etc.)
- Estimate FPS impact if relevant (e.g., "Runs in Tick - ~0.01ms per call")
- For expensive operations: suggest optimizations or note when to be careful

Example format:
```
⚡ Performance: This algorithm is O(n) where n = number of tracked projectiles.
   With typical boss fight (3-5 projectiles), cost is negligible (<0.01ms/frame).
   Would become problematic only with 100+ projectiles.
```

## Naming Mechanics - IMPORTANT

**При реализации новой механики — ВСЕГДА придумывай оригинальное игровое название, а не техническое:**
- Техническое название (для кода): `SuppressionFire`, `VelocityModifier` — остаётся в коде
- Игровое название (для игрока/дизайна): должно быть метафоричным, memorable, неочевидным
- Примеры: `SuppressionFire` → **"Plot Armor"**, `VelocityDamageModifier` → **"Testosterone Boost"**

**Правила хорошего игрового названия:**
1. Передаёт ощущение, а не механику
2. Может быть ироничным, мемным или кинематографичным
3. Игрок должен понять суть по названию, не читая описание
4. Предложи 2-3 варианта и дай пользователю выбрать

**Формат:**
```
🎮 Игровое название: "Plot Armor" / "Ghost Mode" / "Bullet Magnet"
   (техническое: SuppressionFireUpgrade)
```

## English Grammar Correction

**When the user writes prompts in English**, correct major grammar inaccuracies (wrong tense, wrong word, broken sentence structure, etc.). Ignore minor issues like capitalization, missing periods, or stylistic choices.

**Place corrections AFTER the file changes report**, at the very end of the response.

Example format:
```
✏️ Grammar:
- "I putted the function there" → "I put the function there" (irregular past tense)
- "it don't compile" → "it doesn't compile" (subject-verb agreement)
```
