import unreal

CONFIG = {
    "/Game/UI/Screens/WBP_Profile": {
        "TextBindings": {
            "ProfileSummary": "profile.summary",
            "Activity": "profile.activity",
            "StatusText": "connection",
        },
        "EnabledBindings": {"RefreshButton": "idle"},
    },
    "/Game/UI/Screens/WBP_Leaderboard": {
        "TextBindings": {
            "LeaderboardText": "leaderboard",
            "StatusText": "connection",
        },
        "EnabledBindings": {},
    },
    "/Game/UI/Screens/WBP_Results": {
        "TextBindings": {
            "ReportText": "report",
            "StatusText": "connection",
        },
        "EnabledBindings": {},
    },
}

for asset_path, properties in CONFIG.items():
    asset = unreal.load_asset(asset_path)
    if not asset:
        unreal.log_error("Could not load widget asset: {}".format(asset_path))
        continue
    generated_class = asset.generated_class()
    if not generated_class:
        unreal.log_error("Could not get generated class: {}".format(asset_path))
        continue
    cdo = unreal.get_default_object(generated_class)
    for property_name, pairs in properties.items():
        try:
            before = cdo.get_editor_property(property_name)
            desired = {unreal.Name(key): unreal.Name(value) for key, value in pairs.items()}
            cdo.set_editor_property(property_name, desired)
            actual = cdo.get_editor_property(property_name)
            unreal.log("{} {} = {} (was {})".format(asset_path, property_name, actual, before))
        except Exception as exc:
            unreal.log_error("Failed to set {} on {}: {}".format(property_name, asset_path, exc))
    asset.mark_package_dirty()
    unreal.EditorAssetLibrary.save_loaded_asset(asset)
