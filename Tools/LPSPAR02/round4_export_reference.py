import unreal
bp=unreal.load_asset('/Game/InfimaGames/LowPolyShooterPack/Core/Characters/ABP_LPSP_TP_PCH')
print('EXPORT API',unreal.Exporter.run_asset_export_task.__doc__)
task=unreal.AssetExportTask()
task.object=bp
task.filename=unreal.Paths.project_saved_dir()+'LPSP_AR02/infima_reference.t3d'
task.exporter=unreal.ObjectExporterT3D()
task.automated=True
task.prompt=False
task.replace_identical=True
print('EXPORTED',unreal.Exporter.run_asset_export_task(task),task.errors)
