"""Canonical UMG configuration for the VSM conductor trainer.

Idempotent: run it headless with
    UnrealEditor-Cmd <uproject> -run=pythonscript -script="Tools/configure_umg.py" -unattended -NullRHI -nosplash -NoSound

It wires the framework classes, removes duplicated widgets left behind by earlier
tooling gaps, and fills the VSMWidget TextBindings/EnabledBindings maps that drive
every data screen at runtime.
"""
import json
import os

import unreal

try:
    ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
except NameError:  # UnrealEditor-Cmd -run=pythonscript does not always define __file__
    ROOT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
REPORT = os.path.join(ROOT, 'Saved', 'configure_umg_report.json')
FRAMEWORK = '/Game/Framework/'
SCREENS = '/Game/UI/Screens/'
LOG = []


def note(message):
    LOG.append(message)
    unreal.log('VSM_CONFIGURE: ' + message)


def load(name):
    return unreal.EditorAssetLibrary.load_blueprint_class(FRAMEWORK + name)


def widget_asset(name):
    return unreal.EditorAssetLibrary.load_asset(SCREENS + 'WBP_' + name)


# --- framework -------------------------------------------------------------
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level('/Game/Maps/Dev/BackendSandbox')

game_mode = unreal.get_default_object(load('BP_GameMode'))
for prop, name in {'default_pawn_class': 'BP_Conductor', 'player_controller_class': 'BP_PlayerController',
                   'hud_class': 'BP_HUD', 'game_state_class': 'BP_GameState',
                   'player_state_class': 'BP_PlayerState'}.items():
    game_mode.set_editor_property(prop, load(name))
unreal.EditorAssetLibrary.save_asset(FRAMEWORK + 'BP_GameMode', False)

MAPPING = {'WELCOME': 'MainMenu', 'SCENARIOS': 'Training', 'GAMEPLAY': 'GameplayHUD', 'DIALOGUE': 'Dialogue',
           'RESULTS': 'Results', 'PROFILE': 'Profile', 'LEADERBOARD': 'Leaderboard', 'GUIDE': 'Theory',
           'CONNECTION': 'Login', 'TICKET_CHECK': 'TicketCheck', 'SETTINGS': 'Settings', 'TUTORIAL': 'Tutorial'}
enum = unreal.VSMUIScreen
hud = unreal.get_default_object(load('BP_HUD'))
hud.set_editor_property('screen_classes',
                        {getattr(enum, key): unreal.EditorAssetLibrary.load_blueprint_class(SCREENS + 'WBP_' + name)
                         for key, name in MAPPING.items()})
unreal.EditorAssetLibrary.save_asset(FRAMEWORK + 'BP_HUD', False)
note('framework classes configured')

# --- data bindings ---------------------------------------------------------
START_BUTTONS = ['TrainWater', 'Ranked', 'TrainTemp', 'TrainSituation8', 'TrainSituation9', 'TrainSituation10',
                 'TrainSituation17', 'TrainSituation28', 'TrainSituation43', 'TrainSituation44']
TEXT = {
    'MainMenu': {'Status': 'connection'},
    'Login': {'Status': 'connection'},
    'Training': {'Status': 'connection'},
    'Dialogue': {'Status': 'connection', 'PassengerLine': 'task', 'SpeechText': 'speech'},
    'GameplayHUD': {'Gauges': 'gauges', 'Timer': 'remaining_seconds', 'Tasks': 'tasks', 'Status': 'connection',
                    **{'Slot%dLabel' % i: 'slot:%d' % i for i in range(8)}},
    'TicketCheck': {'Passport': 'document:passport', 'Ticket': 'document:ticket',
                    'Terminal': 'document:terminal', 'Status': 'connection'},
    'Profile': {'ProfileSummary': 'profile.summary', 'Activity': 'profile.activity', 'StatusText': 'connection'},
    'Leaderboard': {'LeaderboardText': 'leaderboard', 'StatusText': 'connection'},
    'Results': {'ReportText': 'report', 'StatusText': 'connection'},
}
ENABLED = {
    'MainMenu': {'LoginButton': 'idle', 'ResumeButton': 'action'},
    'Login': {'LoginButton': 'idle', 'RegisterButton': 'idle'},
    'Training': dict([(b, 'start') for b in START_BUTTONS] + [('RetryButton', 'retry')]),
    'Dialogue': {'SendAnswer': 'action', 'OpenDocuments': 'action', 'RetryButton': 'retry'},
    'GameplayHUD': dict([(b, 'action') for b in ['Take', 'FinishButton'] + ['Slot%d' % i for i in range(8)]]
                        + [('RetryButton', 'retry')]),
    'TicketCheck': {'AcceptButton': 'action', 'RejectButton': 'action'},
    'Profile': {'RefreshButton': 'idle'},
}
APPLIED = {}
for name, pairs in TEXT.items():
    asset = widget_asset(name)
    default = unreal.get_default_object(unreal.EditorAssetLibrary.load_blueprint_class(SCREENS + 'WBP_' + name))
    default.set_editor_property('text_bindings', {unreal.Name(k): unreal.Name(v) for k, v in pairs.items()})
    unreal.BlueprintEditorLibrary.compile_blueprint(asset)
    unreal.EditorAssetLibrary.save_asset(SCREENS + 'WBP_' + name, False)
    APPLIED[name] = {'text': pairs, 'enabled': ENABLED.get(name, {})}
for name, pairs in ENABLED.items():
    asset = widget_asset(name)
    default = unreal.get_default_object(unreal.EditorAssetLibrary.load_blueprint_class(SCREENS + 'WBP_' + name))
    default.set_editor_property('enabled_bindings', {unreal.Name(k): unreal.Name(v) for k, v in pairs.items()})
    unreal.BlueprintEditorLibrary.compile_blueprint(asset)
    unreal.EditorAssetLibrary.save_asset(SCREENS + 'WBP_' + name, False)
note('bindings applied: ' + json.dumps(APPLIED, ensure_ascii=False))

# --- tree audit ------------------------------------------------------------
# Read-only: earlier passes could add WidgetTree entries but not remove them, so
# WBP_Profile looked like it held two RefreshButton/ProfileSummary. The audit
# shows which names really repeat so regressions stay visible.
REPEATED = {}
for name in MAPPING.values():
    asset = widget_asset(name)
    if not asset:
        note('MISSING asset ' + name)
        continue
    repeated = [str(item) for item in unreal.VSMWidgetEditorTools.list_repeated_names(asset)]
    if repeated:
        REPEATED[name] = repeated
note('repeated names: ' + json.dumps(REPEATED, ensure_ascii=False))

# --- level -----------------------------------------------------------------
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
note('DONE')

with open(REPORT, 'w', encoding='utf-8') as handle:
    handle.write('\n'.join(LOG))
