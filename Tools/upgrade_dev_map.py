"""Expand only the generated sandbox. Never touches imported production content."""
import unreal

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
level.load_level('/Game/Maps/Dev/BackendSandbox')
cube = unreal.load_asset('/Engine/BasicShapes/Cube.Cube')
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()


def material(name, rgb):
    path = '/Game/Dev/Materials/' + name
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        return unreal.load_asset(path)
    mat = asset_tools.create_asset(name, '/Game/Dev/Materials', unreal.Material, unreal.MaterialFactoryNew())
    expression = unreal.MaterialEditingLibrary.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector)
    expression.set_editor_property('constant', unreal.LinearColor(*rgb, 1))
    unreal.MaterialEditingLibrary.connect_material_property(expression, '', unreal.MaterialProperty.MP_BASE_COLOR)
    unreal.MaterialEditingLibrary.recompile_material(mat)
    unreal.EditorAssetLibrary.save_loaded_asset(mat)
    return mat


skin = material('M_DevSkin', (.64, .39, .24))
clothes = [material('M_DevClothes' + str(i), rgb) for i, rgb in enumerate(((.035,.13,.22),(.26,.08,.09),(.08,.23,.19),(.48,.34,.18)))]
dark = material('M_DevDark', (.025,.03,.045))
window = material('M_DevWindow', (.06,.17,.25))
seat = unreal.load_asset('/Game/Dev/Materials/M_DevSeat')
accent = unreal.load_asset('/Game/Dev/Materials/M_DevAccent')
existing = {a.get_actor_label(): a for a in actors.get_all_level_actors()}


def box(label, pos, scale, mat):
    actor = existing.get(label) or actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*pos))
    actor.set_actor_label(label)
    actor.set_actor_location(unreal.Vector(*pos), False, False)
    actor.set_actor_scale3d(unreal.Vector(*scale))
    actor.static_mesh_component.set_static_mesh(cube)
    actor.static_mesh_component.set_material(0, mat)
    return actor


for index, x in enumerate((0,300,600,900,1200,1500)):
    for side in (-1,1):
        box(f'Window_{index}_{side}', (x,side*337,158), (1.9,.08,.96), window)
        box(f'WindowTrim_{index}_{side}', (x,side*332,213), (2.05,.1,.07), accent)
        for seat_index in (0,1):
            y=side*(188+seat_index*88)
            box(f'DetailedSeat_{index}_{side}_{seat_index}',(x,y,43),(.85,.76,.15),seat)
            box(f'DetailedBack_{index}_{side}_{seat_index}',(x+40,y,86),(.17,.78,.91),seat)
            box(f'Headrest_{index}_{side}_{seat_index}',(x+38,y,137),(.25,.67,.25),seat)
    # The initial broad seats are replaced only by their exact generated labels.
    for side in (-230,230):
        for prefix in ('Seat','Back'):
            old=existing.get(f'{prefix}_{x}_{side}')
            if old:
                actors.destroy_actor(old)
box('LuggageRack',(1560,-240,170),(1.2,1.4,.09),dark)
box('TrainingLuggage',(1490,-150,37),(.5,.36,.7),clothes[1])
cls=unreal.load_class(None,'/Script/VSMConductorTrainer.VSMPassengerCharacter')
names=['Алексей','Марина','Денис','Ольга','Игорь','Анна','Михаил','Елена','Артём','Ирина']
for index in range(10):
    label=f'Passenger_{index+1:02d}_Placeholder'
    npc=existing.get(label) or actors.spawn_actor_from_class(cls,unreal.Vector())
    x=120+(index//2)*300
    y=118 if index%2==0 else -118
    npc.set_actor_label(label)
    npc.set_actor_location(unreal.Vector(x,y,90),False,False)
    npc.set_actor_rotation(unreal.Rotator(pitch=0,yaw=180,roll=0),False)
    npc.set_editor_property('passenger_id',f'passenger_{index+1:02d}')
    npc.set_editor_property('display_name',unreal.Text(names[index]+' · '+str(index+1)))
    for component in npc.get_components_by_class(unreal.StaticMeshComponent):
        name=component.get_name()
        component.set_material(0,skin if 'Head' in name else dark if any(part in name for part in ('Leg','Eye','Hair')) else clothes[index%len(clothes)])
for actor in actors.get_all_level_actors():
    if isinstance(actor,unreal.DirectionalLight):
        actor.set_actor_rotation(unreal.Rotator(pitch=-65,yaw=-25,roll=0),False)
        actor.light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
        actor.light_component.set_editor_property('intensity',2.0)
    if isinstance(actor,unreal.PointLight):
        actor.point_light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
        actor.point_light_component.set_editor_property('intensity',0.0)
        actor.point_light_component.set_editor_property('cast_shadows',False)
    if isinstance(actor,unreal.SkyLight):
        actor.light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
world.get_world_settings().set_editor_property('force_no_precomputed_lighting',True)
fill=existing.get('SoftFill') or actors.spawn_actor_from_class(unreal.DirectionalLight,unreal.Vector(0,0,800))
fill.set_actor_label('SoftFill')
fill.set_actor_rotation(unreal.Rotator(pitch=-35,yaw=150,roll=0),False)
fill.light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
fill.light_component.set_editor_property('intensity',1.2)
fill.light_component.set_editor_property('cast_shadows',False)
ceiling=box('DialogueCeiling',(650,0,310),(21,7,.12),unreal.load_asset('/Game/Dev/Materials/M_DevWall'))
ceiling.set_editor_property('tags',[unreal.Name('VSM.DialogueOnly')])
ceiling.set_actor_hidden_in_game(True)
ceiling.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
ceiling.static_mesh_component.set_editor_property('cast_shadow',False)
backdrop=box('Backdrop',(650,0,-80),(160,160,.2),dark)
backdrop.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
if not level.save_current_level():
    raise RuntimeError('Could not save sandbox map; close other instances using this level')
unreal.log('VSM: detailed primitive carriage and ten passengers saved')
