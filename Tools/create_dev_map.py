"""Run with UE's PythonScript commandlet. Creates real assets through the editor API."""
import unreal

MAP = "/Game/Maps/Dev/BackendSandbox"
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assets = unreal.AssetToolsHelpers.get_asset_tools()

if unreal.EditorAssetLibrary.does_asset_exist(MAP):
    unreal.log("VSM: sandbox already exists; preserving designer edits")
else:
    if not level.new_level(MAP):
        raise RuntimeError("Could not create sandbox map")

    def material(name, color):
        path = "/Game/Dev/Materials/" + name
        mat = unreal.load_asset(path)
        if mat:
            return mat
        mat = assets.create_asset(name, "/Game/Dev/Materials", unreal.Material, unreal.MaterialFactoryNew())
        node = unreal.MaterialEditingLibrary.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector)
        node.set_editor_property("constant", unreal.LinearColor(*color, 1.0))
        unreal.MaterialEditingLibrary.connect_material_property(node, "", unreal.MaterialProperty.MP_BASE_COLOR)
        unreal.MaterialEditingLibrary.recompile_material(mat)
        unreal.EditorAssetLibrary.save_loaded_asset(mat)
        return mat

    floor_mat = material("M_DevFloor", (.055, .085, .11))
    wall_mat = material("M_DevWall", (.58, .65, .68))
    seat_mat = material("M_DevSeat", (.025, .21, .25))
    accent_mat = material("M_DevAccent", (.04, .6, .51))
    cube = unreal.load_asset("/Engine/BasicShapes/Cube.Cube")

    def box(name, loc, scale, mat):
        actor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*loc))
        actor.set_actor_label(name)
        actor.static_mesh_component.set_static_mesh(cube)
        actor.static_mesh_component.set_material(0, mat)
        actor.set_actor_scale3d(unreal.Vector(*scale))
        return actor

    box("Sandbox_Floor", (650, 0, -15), (21, 6.8, .3), floor_mat)
    box("Left_Wall", (650, -350, 140), (21, .2, 3.1), wall_mat)
    box("Right_Wall", (650, 350, 140), (21, .2, 3.1), wall_mat)
    box("End_Wall", (1700, 0, 140), (.2, 7, 3.1), wall_mat)
    box("Start_Wall", (-400, 0, 140), (.2, 7, 3.1), wall_mat)
    box("Aisle_Guide", (650, 0, 1), (20, .05, .015), accent_mat)
    for x in (0, 300, 600, 900, 1200, 1500):
        for y in (-230, 230):
            box(f"Seat_{x}_{y}", (x, y, 42), (1.15, 1.35, .18), seat_mat)
            box(f"Back_{x}_{y}", (x + 48, y, 96), (.18, 1.35, 1.1), seat_mat)

    start = actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(-180, 0, 100), unreal.Rotator(0, 0, 0))
    start.set_actor_label("PlayerStart_Conductor")
    cls = unreal.load_class(None, "/Script/VSMConductorTrainer.VSMPassengerCharacter")
    for index, pos in enumerate(((180, 90, 90), (780, -100, 90)), 1):
        npc = actors.spawn_actor_from_class(cls, unreal.Vector(*pos), unreal.Rotator(0, 180, 0))
        npc.set_editor_property("passenger_id", f"passenger_{index:02d}")
        npc.set_editor_property("display_name", unreal.Text(f"Passenger {index:02d}"))
        npc.set_actor_label(f"Passenger_{index:02d}_Placeholder")
    light = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 500), unreal.Rotator(-65, -25, 0))
    light.light_component.set_editor_property("intensity", 3.0)
    for x in (-150, 500, 1200):
        lamp = actors.spawn_actor_from_class(unreal.PointLight, unreal.Vector(x, 0, 280))
        lamp.point_light_component.set_editor_property("intensity", 3500.0)
        lamp.point_light_component.set_editor_property("attenuation_radius", 750.0)
    sky = actors.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 300))
    sky.light_component.set_editor_property("intensity", .7)
    settings = unreal.EditorLevelLibrary.get_editor_world().get_world_settings()
    settings.set_editor_property("default_game_mode", unreal.load_class(None, "/Script/VSMConductorTrainer.VSMGameMode"))
    level.save_current_level()
    unreal.EditorAssetLibrary.save_directory("/Game/Dev", only_if_is_dirty=False, recursive=True)
    unreal.log("VSM: sandbox map and placeholder materials saved")
