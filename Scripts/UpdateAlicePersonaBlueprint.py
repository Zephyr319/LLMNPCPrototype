import unreal


BLUEPRINT_PATH = "/Game/Blueprints/BP_LLM_NPC_VRM"


blueprint = unreal.load_asset(BLUEPRINT_PATH)
if not blueprint:
    raise RuntimeError(f"Could not load blueprint: {BLUEPRINT_PATH}")

unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)

subobject_system = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
handles = subobject_system.k2_gather_subobject_data_for_blueprint(blueprint)
components = []
for handle in handles:
    data = unreal.SubobjectDataBlueprintFunctionLibrary.get_data(handle)
    component = unreal.SubobjectDataBlueprintFunctionLibrary.get_object_for_blueprint(
        data, blueprint
    )
    if isinstance(component, unreal.NPCProactiveMenuHelpComponent):
        components.append(component)

if not components:
    raise RuntimeError("NPCProactiveMenuHelpComponent template was not found on the NPC blueprint")

for component in components:
    component.set_editor_property("npc_name", "アリス")
    component.set_editor_property("npc_role", "heroic game-loving cafe maid")
    if component.get_editor_property("npc_name") != "アリス":
        raise RuntimeError("Failed to update NPCName on the Blueprint component template")
    if component.get_editor_property("npc_role") != "heroic game-loving cafe maid":
        raise RuntimeError("Failed to update NPCRole on the Blueprint component template")

if not unreal.EditorAssetLibrary.save_loaded_asset(blueprint, only_if_is_dirty=False):
    raise RuntimeError(f"Failed to save blueprint: {BLUEPRINT_PATH}")

unreal.log(
    f"[AlicePersona] updated {len(components)} proactive-help component(s) on {BLUEPRINT_PATH}"
)
