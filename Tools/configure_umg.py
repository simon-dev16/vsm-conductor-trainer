import unreal
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level('/Game/Maps/Dev/BackendSandbox')

framework = '/Game/Framework/'
screens = '/Game/UI/Screens/'
def bp(name):
    return unreal.EditorAssetLibrary.load_blueprint_class(framework+name)

game_mode = unreal.get_default_object(bp('BP_GameMode'))
for prop, name in {'default_pawn_class':'BP_Conductor','player_controller_class':'BP_PlayerController','hud_class':'BP_HUD','game_state_class':'BP_GameState','player_state_class':'BP_PlayerState'}.items():
    game_mode.set_editor_property(prop,bp(name))
unreal.EditorAssetLibrary.save_asset(framework+'BP_GameMode',False)

mapping = {'WELCOME':'MainMenu','SCENARIOS':'Training','GAMEPLAY':'GameplayHUD','DIALOGUE':'Dialogue','RESULTS':'Results','PROFILE':'Profile','LEADERBOARD':'Leaderboard','GUIDE':'Theory','CONNECTION':'Login','TICKET_CHECK':'TicketCheck','SETTINGS':'Settings','TUTORIAL':'Tutorial'}
enum = unreal.VSMUIScreen
screen_classes = {getattr(enum,key): unreal.EditorAssetLibrary.load_blueprint_class(screens+'WBP_'+name) for key,name in mapping.items()}
hud = unreal.get_default_object(bp('BP_HUD'))
hud.set_editor_property('screen_classes',screen_classes)
unreal.EditorAssetLibrary.save_asset(framework+'BP_HUD',False)

bindings = {
 'GameplayHUD':{'Gauges':'gauges','Timer':'remaining_seconds','Tasks':'tasks','Status':'message',**{f'Slot{i}Label':f'slot:{i}' for i in range(8)}},
 'Login':{'Status':'message'}, 'Training':{'Status':'message'},
 'Dialogue':{'Status':'message','PassengerLine':'task','SpeechText':'speech'},
}
for name,values in bindings.items():
    asset = screens+'WBP_'+name
    default = unreal.get_default_object(unreal.EditorAssetLibrary.load_blueprint_class(asset))
    default.set_editor_property('text_bindings',values)
    unreal.EditorAssetLibrary.save_asset(asset,False)
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
world.get_world_settings().set_editor_property('default_game_mode',bp('BP_GameMode'))
level.save_current_level()
unreal.log('VSM_FRAMEWORK_CONFIGURED: 7 Blueprint framework classes, 12 UMG screens')

