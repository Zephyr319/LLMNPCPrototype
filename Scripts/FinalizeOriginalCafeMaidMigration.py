import unreal


ROOT = "/Game/Characters/OriginalCafeMaid"
ANIMATION_ROOT = f"{ROOT}/Animations"
NPC_BLUEPRINT_PATH = "/Game/Blueprints/BP_LLM_NPC_VRM"
NPC_BLUEPRINT_BACKUP_PATH = (
    "/Game/Blueprints/Backup/BP_LLM_NPC_VRM_PreOriginalMaid"
)
ACTION_SET_PATH = "/Game/Blueprints/Cafe/DA_NPCAutonomyActionSet"

OLD_MESH_PATH = "/Game/art/虚拟女仆/SK_（25_9_9更新）midars"
NEW_MESH_PATH = f"{ROOT}/SK_OriginalCafeMaid_v1_0"
OLD_ANIM_BP_PATH = "/Game/art/虚拟女仆/动画/ABP_ArisNPC"
NEW_ANIM_BP_PATH = f"{ANIMATION_ROOT}/ABP_OriginalCafeMaid"

OLD_MONTAGE_PATHS = [
    "/Game/art/虚拟女仆/TM_Explain",
    "/Game/art/虚拟女仆/TM_Greet",
    "/Game/art/虚拟女仆/TM_Recommend",
    "/Game/art/虚拟女仆/TM_serve_food",
]
NEW_MONTAGE_PATHS = [
    f"{ANIMATION_ROOT}/AM_Explain",
    f"{ANIMATION_ROOT}/AM_Greet",
    f"{ANIMATION_ROOT}/AM_Recommend",
    f"{ANIMATION_ROOT}/AM_ServeFood",
]

EXPLICIT_RENAMES = {
    "ABP_ArisNPC_OCM": "ABP_OriginalCafeMaid",
    "TM_Explain_OCM": "AM_Explain",
    "TM_Greet_OCM": "AM_Greet",
    "TM_Recommend_OCM": "AM_Recommend",
    "TM_serve_food_OCM": "AM_ServeFood",
    "Angry_OCM": "Anim_Angry_OCM",
    "Nervously_Look_Around_OCM": "Anim_Nervously_Look_Around_OCM",
    "Pick_up_OCM": "Anim_PickUpDishes_OCM",
    "Reacting_OCM": "Anim_Reacting_OCM",
    "Searching_Files_High_OCM": "Anim_Searching_Files_High_OCM",
    "Standing_Arguing_Anim_OCM": "Anim_Standing_Arguing_OCM",
    "Standing_Using_Touchscreen_Tablet_OCM": (
        "Anim_Standing_Using_Touchscreen_Tablet_OCM"
    ),
    "Surprised_OCM": "Anim_Surprised_OCM",
    "Telling_A_Secret_OCM": "Anim_Telling_A_Secret_OCM",
    "Texting_OCM": "Anim_Texting_OCM",
    "Texting_And_Walking_OCM": "Anim_Texting_And_Walking_OCM",
    "Texting_While_Standing_OCM": "Anim_Texting_While_Standing_OCM",
    "Watering_Anim_OCM": "Anim_WaterPlant_OCM",
    "pick_up_table_OCM": "Anim_CleanCounter_OCM",
    "pick_up_table__2__OCM": "Anim_WipeDiningTable_OCM",
    "Yawn_OCM": "Anim_Yawn_OCM",
}

ACTION_ANIMATION_PATHS = {
    "phone_at_counter": f"{ANIMATION_ROOT}/Anim_Texting_While_Standing_OCM",
    "daydream_at_counter": f"{ANIMATION_ROOT}/Anim_KA_Idle04_LookAtFeet_OCM",
    "look_around": f"{ANIMATION_ROOT}/Anim_KA_Idle02_LookLeftAndRight_OCM",
    "doze_at_counter": f"{ANIMATION_ROOT}/Anim_Yawn_OCM",
    "water_flower": f"{ANIMATION_ROOT}/Anim_WaterPlant_OCM",
    "check_supplies": f"{ANIMATION_ROOT}/Anim_Searching_Files_High_OCM",
    "review_shift_notes": (
        f"{ANIMATION_ROOT}/Anim_Standing_Using_Touchscreen_Tablet_OCM"
    ),
    "angry_at_counter": f"{ANIMATION_ROOT}/Anim_Angry_OCM",
    "hum_at_counter_placeholder": (
        f"{ANIMATION_ROOT}/Anim_KA_Idle08_ComeUpWithAnIdea_OCM"
    ),
    "straighten_uniform_placeholder": (
        f"{ANIMATION_ROOT}/Anim_KA_Idle03_LookAtHands_OCM"
    ),
    "inspect_cafe_placeholder": (
        f"{ANIMATION_ROOT}/Anim_KA_Idle02_LookLeftAndRight_OCM"
    ),
    "organize_counter_placeholder": f"{ANIMATION_ROOT}/Anim_CleanCounter_OCM",
    "inspect_table_area": (
        f"{ANIMATION_ROOT}/Anim_KA_Idle02_LookLeftAndRight_OCM"
    ),
    "tidy_table_area": f"{ANIMATION_ROOT}/Anim_WipeDiningTable_OCM",
    "collect_tableware": f"{ANIMATION_ROOT}/Anim_PickUpDishes_OCM",
}


def require_asset(path):
    asset = unreal.load_asset(path)
    if not asset:
        raise RuntimeError(f"Missing required asset: {path}")
    return asset


def move_generated_assets():
    unreal.EditorAssetLibrary.make_directory(ANIMATION_ROOT)
    root_assets = unreal.EditorAssetLibrary.list_assets(
        "/Game", recursive=False, include_folder=False
    )
    moved = 0
    for object_path in root_assets:
        asset = unreal.load_asset(object_path)
        if not asset or not asset.get_name().endswith("_OCM"):
            continue

        source_path = f"/Game/{asset.get_name()}"
        destination_name = EXPLICIT_RENAMES.get(asset.get_name(), asset.get_name())
        destination_path = f"{ANIMATION_ROOT}/{destination_name}"
        if unreal.EditorAssetLibrary.does_asset_exist(destination_path):
            unreal.log_warning(
                f"[OriginalMaidFinalize] move skipped; destination exists={destination_path}"
            )
            continue
        if not unreal.EditorAssetLibrary.rename_asset(source_path, destination_path):
            raise RuntimeError(f"Could not move {source_path} -> {destination_path}")
        moved += 1
        unreal.log_warning(
            f"[OriginalMaidFinalize] moved={source_path} -> {destination_path}"
        )
    unreal.log_warning(f"[OriginalMaidFinalize] moved_count={moved}")


def compile_blueprint(path):
    blueprint = require_asset(path)
    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    unreal.EditorAssetLibrary.save_loaded_asset(blueprint, only_if_is_dirty=False)
    unreal.log_warning(f"[OriginalMaidFinalize] compiled={path}")
    return blueprint


def backup_npc_blueprint():
    if unreal.EditorAssetLibrary.does_asset_exist(NPC_BLUEPRINT_BACKUP_PATH):
        unreal.log_warning(
            f"[OriginalMaidFinalize] backup already exists={NPC_BLUEPRINT_BACKUP_PATH}"
        )
        return
    backup = unreal.EditorAssetLibrary.duplicate_asset(
        NPC_BLUEPRINT_PATH, NPC_BLUEPRINT_BACKUP_PATH
    )
    if not backup:
        raise RuntimeError("Could not create NPC blueprint backup")
    unreal.EditorAssetLibrary.save_loaded_asset(backup, only_if_is_dirty=False)
    unreal.log_warning(
        f"[OriginalMaidFinalize] backup_created={NPC_BLUEPRINT_BACKUP_PATH}"
    )


def replace_npc_blueprint_references(new_anim_bp):
    npc_blueprint = require_asset(NPC_BLUEPRINT_PATH)
    old_anim_bp = require_asset(OLD_ANIM_BP_PATH)
    old_objects = [
        require_asset(OLD_MESH_PATH),
        old_anim_bp,
        old_anim_bp.generated_class(),
    ]
    new_objects = [
        require_asset(NEW_MESH_PATH),
        new_anim_bp,
        new_anim_bp.generated_class(),
    ]
    for old_path, new_path in zip(OLD_MONTAGE_PATHS, NEW_MONTAGE_PATHS):
        old_objects.append(require_asset(old_path))
        new_objects.append(require_asset(new_path))

    replacement_count = (
        unreal.AssetReferenceReplacementLibrary.replace_object_references(
            npc_blueprint, old_objects, new_objects
        )
    )

    subsystem = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    handles = subsystem.k2_gather_subobject_data_for_blueprint(npc_blueprint)
    component_updated = False
    for handle in handles:
        data = unreal.SubobjectDataBlueprintFunctionLibrary.get_data(handle)
        component = (
            unreal.SubobjectDataBlueprintFunctionLibrary.get_object_for_blueprint(
                data, npc_blueprint
            )
        )
        if not isinstance(component, unreal.SkeletalMeshComponent):
            continue
        component.set_editor_property(
            "skeletal_mesh_asset", require_asset(NEW_MESH_PATH)
        )
        component.set_editor_property("anim_class", new_anim_bp.generated_class())
        component.modify()
        component_updated = True

    if not component_updated:
        raise RuntimeError("NPC skeletal mesh component was not found")

    unreal.BlueprintEditorLibrary.compile_blueprint(npc_blueprint)
    unreal.EditorAssetLibrary.save_loaded_asset(
        npc_blueprint, only_if_is_dirty=False
    )
    unreal.log_warning(
        f"[OriginalMaidFinalize] npc_updated references={replacement_count} "
        f"component_updated={component_updated}"
    )


def update_action_set():
    action_set = require_asset(ACTION_SET_PATH)
    activities = action_set.get_editor_property("activities")
    updated = 0
    for activity in activities:
        activity_id = str(activity.get_editor_property("activity_id"))
        animation_path = ACTION_ANIMATION_PATHS.get(activity_id)
        if not animation_path:
            raise RuntimeError(f"No migrated animation mapping for {activity_id}")
        activity.set_editor_property("animation", require_asset(animation_path))
        updated += 1

    action_set.set_editor_property("activities", activities)
    action_set.modify()
    unreal.EditorAssetLibrary.save_loaded_asset(action_set, only_if_is_dirty=False)
    unreal.log_warning(f"[OriginalMaidFinalize] action_set_updated={updated}")


def main():
    move_generated_assets()
    new_anim_bp = compile_blueprint(NEW_ANIM_BP_PATH)
    backup_npc_blueprint()
    replace_npc_blueprint_references(new_anim_bp)
    update_action_set()
    unreal.EditorAssetLibrary.save_directory(
        ROOT, only_if_is_dirty=False, recursive=True
    )
    unreal.log_warning("[OriginalMaidFinalize] COMPLETE")


try:
    main()
finally:
    unreal.SystemLibrary.quit_editor()
