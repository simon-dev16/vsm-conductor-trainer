"""Install the passenger face adapter and rebuild the ten existing sandbox passengers.
Run with Unreal's Python Script commandlet after compiling the editor target.
The original MetaHuman assets and all passenger IDs/transforms are preserved.
"""
import json
from pathlib import Path
import unreal

path = '/Game/Passengers/ABP_PassengerFace_PostProcess'
if unreal.EditorAssetLibrary.does_asset_exist(path):
    asset = unreal.load_asset(path)
else:
    asset = unreal.EditorAssetLibrary.duplicate_asset(
        '/Game/MetaHumans/Common/Face/ABP_Face_PostProcess', path)
assert asset, 'Face blueprint duplicate failed'
assert unreal.VSMPassengerEditorTools.add_passenger_gaze(asset), 'Gaze graph compilation failed'
assert unreal.EditorAssetLibrary.save_loaded_asset(asset), 'Face graph save failed'

levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert levels.load_level('/Game/Maps/Dev/BackendSandbox')
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
report = []
for actor in actors.get_all_level_actors():
    if not isinstance(actor, unreal.VSMPassengerCharacter):
        continue
    unreal.VSMPassengerEditorTools.rebuild_passenger(actor)
    visual = actor.get_editor_property('passenger_visual').get_editor_property('child_actor')
    assert visual, actor.get_actor_label()
    report.append({
        'id': actor.get_editor_property('passenger_id'),
        'appearance': actor.get_editor_property('appearance_index'),
        'class': visual.get_class().get_path_name(),
        'location': str(actor.get_actor_location()),
    })
assert len(report) == 10, f'Expected ten sandbox passengers, found {len(report)}'
assert levels.save_current_level(), 'Map save failed'
report_path = Path(unreal.Paths.project_saved_dir()) / 'Verification' / 'passenger-setup.json'
report_path.parent.mkdir(parents=True, exist_ok=True)
report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.log('VSM_PASSENGER_SETUP_OK')
