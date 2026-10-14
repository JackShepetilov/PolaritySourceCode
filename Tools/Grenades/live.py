import unreal


def compile():
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    unreal.SystemLibrary.execute_console_command(world, "LiveCoding.Compile")
    print("live coding asked")
