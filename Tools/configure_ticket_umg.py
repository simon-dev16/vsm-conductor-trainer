import unreal

ASSET_PATH = "/Game/UI/Screens/WBP_TicketCheck"

blueprint = unreal.load_asset(ASSET_PATH)
if not blueprint:
    raise RuntimeError("Could not load " + ASSET_PATH)

cdo = blueprint.generated_class.get_default_object()
text_bindings = {
    unreal.Name("Passport"): "document:passport",
    unreal.Name("Ticket"): "document:ticket",
    unreal.Name("Terminal"): "document:terminal",
    unreal.Name("Status"): "connection",
}
cdo.set_editor_property("TextBindings", text_bindings)

# EnabledBindings is supplied by the native VSMWidget update. This script can
# be run after that build; skip the optional map if the editor is still on the
# previous native class version.
try:
    cdo.set_editor_property("EnabledBindings", {
        unreal.Name("AcceptButton"): "action",
        unreal.Name("RejectButton"): "action",
    })
except Exception as exc:
    unreal.log_warning("EnabledBindings was not applied (native class may need rebuilding): {}".format(exc))

unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
unreal.EditorAssetLibrary.save_loaded_asset(blueprint)
unreal.log("WBP_TicketCheck TextBindings configured and asset saved")
