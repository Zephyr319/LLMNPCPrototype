import unreal


NPC_BLUEPRINT_PATH = "/Game/Blueprints/BP_LLM_NPC_VRM"
OLD_MESH_PATH = "/Game/art/虚拟女仆/SK_（25_9_9更新）midars"
NEW_MESH_PATH = "/Game/Characters/OriginalCafeMaid/SK_OriginalCafeMaid_v1_0"
OLD_ANIM_BP_PATH = "/Game/art/虚拟女仆/动画/ABP_ArisNPC"
ACTION_SET_PATH = "/Game/Blueprints/Cafe/DA_NPCAutonomyActionSet"


def asset_path(asset):
    return asset.get_path_name() if asset else "None"


def log_property(obj, property_name):
    try:
        value = obj.get_editor_property(property_name)
    except Exception as exc:
        unreal.log_warning(
            f"[OriginalMaidAudit] property {property_name}=<unavailable: {exc}>"
        )
        return
    if hasattr(value, "get_path_name"):
        value = value.get_path_name()
    unreal.log_warning(f"[OriginalMaidAudit] property {property_name}={value}")


def audit_mesh(label, path):
    mesh = unreal.load_asset(path)
    if not mesh:
        raise RuntimeError(f"Missing {label} mesh: {path}")

    skeleton = mesh.get_editor_property("skeleton")
    morph_names = sorted(str(name) for name in mesh.get_all_morph_target_names())
    unreal.log_warning(
        f"[OriginalMaidAudit] {label}_mesh={asset_path(mesh)} "
        f"skeleton={asset_path(skeleton)} morph_count={len(morph_names)}"
    )
    unreal.log_warning(
        f"[OriginalMaidAudit] {label}_morphs={','.join(morph_names)}"
    )
    return mesh


def audit_blueprint_components():
    blueprint = unreal.load_asset(NPC_BLUEPRINT_PATH)
    if not blueprint:
        raise RuntimeError(f"Missing NPC blueprint: {NPC_BLUEPRINT_PATH}")

    subsystem = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    handles = subsystem.k2_gather_subobject_data_for_blueprint(blueprint)
    unreal.log_warning(
        f"[OriginalMaidAudit] npc_blueprint={NPC_BLUEPRINT_PATH} "
        f"subobject_count={len(handles)}"
    )

    for handle in handles:
        data = unreal.SubobjectDataBlueprintFunctionLibrary.get_data(handle)
        component = unreal.SubobjectDataBlueprintFunctionLibrary.get_object_for_blueprint(
            data, blueprint
        )
        if not isinstance(component, unreal.SkeletalMeshComponent):
            continue

        mesh = component.get_editor_property("skeletal_mesh_asset")
        anim_class = component.get_editor_property("anim_class")
        unreal.log_warning(
            f"[OriginalMaidAudit] skeletal_component={component.get_name()} "
            f"mesh={asset_path(mesh)} anim_class={asset_path(anim_class)}"
        )


def audit_animation_blueprint():
    anim_bp = unreal.load_asset(OLD_ANIM_BP_PATH)
    if not anim_bp:
        raise RuntimeError(f"Missing animation blueprint: {OLD_ANIM_BP_PATH}")

    unreal.log_warning(f"[OriginalMaidAudit] anim_blueprint={asset_path(anim_bp)}")
    log_property(anim_bp, "target_skeleton")


def audit_action_set():
    action_set = unreal.load_asset(ACTION_SET_PATH)
    if not action_set:
        raise RuntimeError(f"Missing action set: {ACTION_SET_PATH}")

    activities = action_set.get_editor_property("activities")
    unreal.log_warning(
        f"[OriginalMaidAudit] action_set={ACTION_SET_PATH} count={len(activities)}"
    )
    for activity in activities:
        animation = activity.get_editor_property("animation")
        skeleton = (
            animation.get_editor_property("skeleton") if animation else None
        )
        unreal.log_warning(
            f"[OriginalMaidAudit] action={activity.get_editor_property('activity_id')} "
            f"animation={asset_path(animation)} skeleton={asset_path(skeleton)}"
        )


def main():
    audit_mesh("old", OLD_MESH_PATH)
    audit_mesh("new", NEW_MESH_PATH)
    audit_blueprint_components()
    audit_animation_blueprint()
    audit_action_set()
    unreal.log_warning("[OriginalMaidAudit] COMPLETE")


main()
