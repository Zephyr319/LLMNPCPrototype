import unreal


MAP_PATH = "/Game/RestaurantDemo/Maps/Map_RestaurantDemo"
NPC_BLUEPRINT_PATH = "/Game/Blueprints/BP_LLM_NPC_VRM"
NEW_MESH_PATH = "/Game/Characters/OriginalCafeMaid/SK_OriginalCafeMaid_v1_0"
NEW_ANIM_BP_PATH = (
    "/Game/Characters/OriginalCafeMaid/Animations/ABP_OriginalCafeMaid"
)


world = unreal.EditorLoadingAndSavingUtils.load_map(MAP_PATH)
if not world:
    raise RuntimeError(f"Could not load map: {MAP_PATH}")

npc_blueprint = unreal.load_asset(NPC_BLUEPRINT_PATH)
new_mesh = unreal.load_asset(NEW_MESH_PATH)
new_anim_bp = unreal.load_asset(NEW_ANIM_BP_PATH)
if not npc_blueprint or not new_mesh or not new_anim_bp:
    raise RuntimeError("Missing NPC migration asset")

npc_class = npc_blueprint.generated_class()
expected_anim_class = new_anim_bp.generated_class()
npc_actors = [
    actor
    for actor in unreal.EditorLevelLibrary.get_all_level_actors()
    if actor.get_class() == npc_class
]
if not npc_actors:
    raise RuntimeError("No BP_LLM_NPC_VRM instance found in restaurant map")

for actor in npc_actors:
    components = actor.get_components_by_class(unreal.SkeletalMeshComponent)
    matching_components = []
    for component in components:
        mesh = component.get_editor_property("skeletal_mesh_asset")
        if mesh == new_mesh:
            matching_components.append(component)
    if not matching_components:
        raise RuntimeError(
            f"Placed NPC still overrides the migrated mesh: {actor.get_path_name()}"
        )
    for component in matching_components:
        anim_class = component.get_editor_property("anim_class")
        if anim_class != expected_anim_class:
            raise RuntimeError(
                f"Placed NPC AnimBP mismatch: {actor.get_path_name()} -> "
                f"{anim_class.get_path_name() if anim_class else 'None'}"
            )
    unreal.log_warning(
        f"[OriginalMaidWorld] actor=PASS path={actor.get_path_name()} "
        f"components={len(matching_components)}"
    )

unreal.log_warning(
    f"[OriginalMaidWorld] ALL_CHECKS_PASS actor_count={len(npc_actors)}"
)
