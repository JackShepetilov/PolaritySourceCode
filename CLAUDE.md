# Claude Code Rules for Polarity Project

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
