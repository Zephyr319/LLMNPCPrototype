import unreal


ROOT = "/Game/Characters/OriginalCafeMaid"
ANIMATION_ROOT = f"{ROOT}/Animations"
NPC_BLUEPRINT_PATH = "/Game/Blueprints/BP_LLM_NPC_VRM"
ACTION_SET_PATH = "/Game/Blueprints/Cafe/DA_NPCAutonomyActionSet"
NEW_MESH_PATH = f"{ROOT}/SK_OriginalCafeMaid_v1_0"
NEW_SKELETON_PATH = f"{ROOT}/SKEL_OriginalCafeMaid_v1_0"
NEW_ANIM_BP_PATH = f"{ANIMATION_ROOT}/ABP_OriginalCafeMaid"
OLD_PACKAGES = {
    "/Game/art/虚拟女仆/SK_（25_9_9更新）midars",
    "/Game/art/虚拟女仆/动画/ABP_ArisNPC",
    "/Game/art/虚拟女仆/TM_Explain",
    "/Game/art/虚拟女仆/TM_Greet",
    "/Game/art/虚拟女仆/TM_Recommend",
    "/Game/art/虚拟女仆/TM_serve_food",
}
SERVICE_MONTAGES = [
    f"{ANIMATION_ROOT}/AM_Explain",
    f"{ANIMATION_ROOT}/AM_Greet",
    f"{ANIMATION_ROOT}/AM_Recommend",
    f"{ANIMATION_ROOT}/AM_ServeFood",
]


def require_asset(path):
    asset = unreal.load_asset(path)
    if not asset:
        raise RuntimeError(f"Missing required asset: {path}")
    return asset


def path_of(asset):
    return asset.get_path_name() if asset else "None"


def audit_component(npc_blueprint, new_mesh, new_anim_bp):
    subsystem = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    handles = subsystem.k2_gather_subobject_data_for_blueprint(npc_blueprint)
    components = []
    for handle in handles:
        data = unreal.SubobjectDataBlueprintFunctionLibrary.get_data(handle)
        component = (
            unreal.SubobjectDataBlueprintFunctionLibrary.get_object_for_blueprint(
                data, npc_blueprint
            )
        )
        if isinstance(component, unreal.SkeletalMeshComponent):
            components.append(component)

    if len(components) != 1:
        raise RuntimeError(f"Expected one skeletal component, found {len(components)}")

    component = components[0]
    mesh = component.get_editor_property("skeletal_mesh_asset")
    anim_class = component.get_editor_property("anim_class")
    expected_anim_class = new_anim_bp.generated_class()
    if mesh != new_mesh:
        raise RuntimeError(f"NPC mesh mismatch: {path_of(mesh)}")
    if anim_class != expected_anim_class:
        raise RuntimeError(f"NPC AnimBP mismatch: {path_of(anim_class)}")
    unreal.log_warning(
        f"[OriginalMaidResult] component=PASS mesh={path_of(mesh)} "
        f"anim_class={path_of(anim_class)}"
    )


def audit_dependencies():
    options = unreal.AssetRegistryDependencyOptions()
    for property_name in (
        "include_editor_only_package_references",
        "include_game_package_references",
        "include_hard_package_references",
        "include_soft_package_references",
        "include_hard_management_references",
        "include_soft_management_references",
    ):
        options.set_editor_property(property_name, True)
    options.set_editor_property("include_searchable_names", False)
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    dependencies = {
        str(value)
        for value in (registry.get_dependencies(NPC_BLUEPRINT_PATH, options) or [])
    }
    remaining = sorted(OLD_PACKAGES.intersection(dependencies))
    if remaining:
        raise RuntimeError(f"NPC still depends on old assets: {remaining}")
    unreal.log_warning(
        f"[OriginalMaidResult] old_direct_dependencies=PASS total={len(dependencies)}"
    )


def audit_animation_blueprint(new_skeleton):
    anim_bp = require_asset(NEW_ANIM_BP_PATH)
    target_skeleton = anim_bp.get_editor_property("target_skeleton")
    if target_skeleton != new_skeleton:
        raise RuntimeError(
            f"Animation blueprint skeleton mismatch: {path_of(target_skeleton)}"
        )
    unreal.BlueprintEditorLibrary.compile_blueprint(anim_bp)
    unreal.log_warning(
        f"[OriginalMaidResult] anim_blueprint=PASS skeleton={path_of(target_skeleton)}"
    )
    return anim_bp


def audit_service_montages(new_skeleton):
    for path in SERVICE_MONTAGES:
        montage = require_asset(path)
        skeleton = montage.get_editor_property("skeleton")
        if skeleton != new_skeleton:
            raise RuntimeError(f"Montage skeleton mismatch: {path} -> {path_of(skeleton)}")
        unreal.log_warning(
            f"[OriginalMaidResult] service_montage=PASS asset={path}"
        )


def audit_action_set(new_skeleton):
    action_set = require_asset(ACTION_SET_PATH)
    activities = action_set.get_editor_property("activities")
    if len(activities) != 15:
        raise RuntimeError(f"Expected 15 activities, found {len(activities)}")
    for activity in activities:
        activity_id = str(activity.get_editor_property("activity_id"))
        animation = activity.get_editor_property("animation")
        if not animation:
            raise RuntimeError(f"Missing animation for {activity_id}")
        skeleton = animation.get_editor_property("skeleton")
        if skeleton != new_skeleton:
            raise RuntimeError(
                f"Action skeleton mismatch: {activity_id} -> {path_of(skeleton)}"
            )
        unreal.log_warning(
            f"[OriginalMaidResult] action=PASS id={activity_id} "
            f"animation={path_of(animation)}"
        )
    unreal.log_warning(
        f"[OriginalMaidResult] action_set=PASS count={len(activities)}"
    )


def main():
    new_mesh = require_asset(NEW_MESH_PATH)
    new_skeleton = require_asset(NEW_SKELETON_PATH)
    new_anim_bp = audit_animation_blueprint(new_skeleton)
    audit_service_montages(new_skeleton)
    npc_blueprint = require_asset(NPC_BLUEPRINT_PATH)
    unreal.BlueprintEditorLibrary.compile_blueprint(npc_blueprint)
    audit_component(npc_blueprint, new_mesh, new_anim_bp)
    audit_dependencies()
    audit_action_set(new_skeleton)
    unreal.log_warning("[OriginalMaidResult] ALL_CHECKS_PASS")


main()
