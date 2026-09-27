import unreal
import json
from pathlib import Path
world_config=json.loads((Path(__file__).resolve().parents[1]/"Contracts/world-actions.json").read_text(encoding="utf-8-sig"))

level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
level.load_level('/Game/Maps/Dev/BackendSandbox')
assets=unreal.EditorAssetLibrary
factory=unreal.BlueprintFactory()
factory.set_editor_property('parent_class',unreal.VSMWorldObject)
path='/Game/Scenario/World/BP_WorldStation'
if not assets.does_asset_exist(path):
    unreal.AssetToolsHelpers.get_asset_tools().create_asset('BP_WorldStation','/Game/Scenario/World',unreal.Blueprint,factory)
assets.save_asset(path,False)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
existing=next((a for a in actors.get_all_level_actors() if a.get_actor_label()=='Station_Conductor'),None)
if existing is None or isinstance(existing,unreal.StaticMeshActor):
    point=world_config['station']['position']
    actor=actors.spawn_actor_from_class(assets.load_blueprint_class(path),unreal.Vector(point[0],point[1],point[2]-50))
    if not actor:
        raise RuntimeError('World station spawn failed')
    if existing:
        actors.destroy_actor(existing)
    actor.set_actor_label('Station_Conductor')
    actor.get_editor_property('presenter').set_editor_property('world_id',world_config['station']['id'])

framework='/Game/Framework/'
def cls(name):
    return assets.load_blueprint_class(framework+name)
gm=unreal.get_default_object(cls('BP_GameMode'))
for prop,name in {'default_pawn_class':'BP_Conductor','player_controller_class':'BP_PlayerController','hud_class':'BP_HUD','game_state_class':'BP_GameState','player_state_class':'BP_PlayerState'}.items():
    current=gm.get_editor_property(prop)
    if current is None or current.get_path_name().startswith('/Script/'):
        gm.set_editor_property(prop,cls(name))
assets.save_asset(framework+'BP_GameMode',False)
hud=unreal.get_default_object(cls('BP_HUD'))
mapping=dict(hud.get_editor_property('screen_classes'))
for stale in ('SCENARIOS', 'SETTINGS'):
    mapping.pop(getattr(unreal.VSMUIScreen, stale), None)
for key,name in {'WELCOME':'MainMenu','GAMEPLAY':'GameplayHUD','DIALOGUE':'Dialogue','RESULTS':'Results','PROFILE':'Profile','LEADERBOARD':'Leaderboard','GUIDE':'Theory','CONNECTION':'Login','TICKET_CHECK':'TicketCheck','TUTORIAL':'Tutorial'}.items():
    enum=getattr(unreal.VSMUIScreen,key)
    if enum not in mapping:
        mapping[enum]=assets.load_blueprint_class('/Game/UI/Screens/WBP_'+name)
hud.set_editor_property('screen_classes',mapping)
assets.save_asset(framework+'BP_HUD',False)
level.save_current_level()
unreal.log('VSM_WORLD_READY: BP_WorldStation saved, native framework fallbacks replaced, missing UMG slots filled')

bindings={
    'GameplayHUD': {'Gauges':'gauges','Timer':'timer','TaskIndicators':'tasks_compact','Status':'message',**{f'Slot{i}Label':f'slot:{i}' for i in range(8)}},
    'Dialogue': {'PassengerLine':'task','Status':'message'},
    'Login': {'Status':'message'}
}
for name,defaults in bindings.items():
    widget_path='/Game/UI/Screens/WBP_'+name
    widget=unreal.get_default_object(assets.load_blueprint_class(widget_path))
    actual=dict(widget.get_editor_property('text_bindings'))
    for key,value in defaults.items():
        if key not in actual:
            actual[key]=value
    widget.set_editor_property('text_bindings',actual)
    assets.save_asset(widget_path,False)
welcome=mapping.get(unreal.VSMUIScreen.WELCOME)
if welcome and welcome.get_name()=='WBP_Login_C':
    mapping[unreal.VSMUIScreen.WELCOME]=assets.load_blueprint_class('/Game/UI/Screens/WBP_MainMenu')
    hud.set_editor_property('screen_classes',mapping)
    assets.save_asset(framework+'BP_HUD',False)
