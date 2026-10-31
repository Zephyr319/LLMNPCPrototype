import unreal


ROOT = "/Game/Characters/OriginalCafeMaid"
NEW_MESH_PATH = f"{ROOT}/SK_OriginalCafeMaid_v1_0"
NEW_IK_RIG_PATH = f"{ROOT}/IK_OriginalCafeMaid_v1_0_Mannequin"
BASE_NEW_RETARGETER_PATH = f"{ROOT}/RTG_OriginalCafeMaid_v1_0"

OLD_MESH_PATH = "/Game/art/虚拟女仆/SK_（25_9_9更新）midars"
OLD_IK_RIG_PATH = "/Game/art/虚拟女仆/IK_（25_9_9更新）midars_Mannequin"
LEGACY_RETARGETER_PATH = f"{ROOT}/RTG_LegacyMaid_To_OriginalCafeMaid"

MIXAMO_MESH_PATH = "/Game/art/actions/Watering"
MIXAMO_IK_RIG_PATH = "/Game/art/actions/IK_Mixamo_Watering"
MIXAMO_RETARGETER_PATH = f"{ROOT}/RTG_Mixamo_To_OriginalCafeMaid"

LEGACY_ASSET_PATHS = [
    "/Game/art/虚拟女仆/动画/ABP_ArisNPC",
    "/Game/art/虚拟女仆/TM_Explain",
    "/Game/art/虚拟女仆/TM_Greet",
    "/Game/art/虚拟女仆/TM_Recommend",
    "/Game/art/虚拟女仆/TM_serve_food",
    "/Game/art/虚拟女仆/Anim_KA_Idle02_LookLeftAndRight",
    "/Game/art/虚拟女仆/Anim_KA_Idle03_LookAtHands",
    "/Game/art/虚拟女仆/Anim_KA_Idle04_LookAtFeet",
    "/Game/art/虚拟女仆/Anim_KA_Idle08_ComeUpWithAnIdea",
    "/Game/art/虚拟女仆/Anim_KA_Idle12_LeaningForward",
]

MIXAMO_ASSET_PATHS = [
    "/Game/art/actions/Angry",
    "/Game/art/actions/Nervously_Look_Around",
    "/Game/art/actions/Pick_up",
    "/Game/art/actions/Reacting",
    "/Game/art/actions/Searching_Files_High",
    "/Game/art/actions/Standing_Arguing_Anim",
    "/Game/art/actions/Standing_Using_Touchscreen_Tablet",
    "/Game/art/actions/Surprised",
    "/Game/art/actions/Telling_A_Secret",
    "/Game/art/actions/Texting",
    "/Game/art/actions/Texting_And_Walking",
    "/Game/art/actions/Texting_While_Standing",
    "/Game/art/actions/Watering_Anim",
    "/Game/art/actions/pick_up_table",
    "/Game/art/actions/pick_up_table__2_",
    "/Game/art/actions/Yawn",
]


def require_asset(path):
    asset = unreal.load_asset(path)
    if not asset:
        raise RuntimeError(f"Missing required asset: {path}")
    return asset


def duplicate_or_load(source_path, destination_path):
    existing = unreal.load_asset(destination_path)
    if existing:
        return existing

    duplicated = unreal.EditorAssetLibrary.duplicate_asset(
        source_path, destination_path
    )
    if not duplicated:
        raise RuntimeError(
            f"Could not duplicate retargeter {source_path} -> {destination_path}"
        )
    return duplicated


def configure_retargeter(destination_path, source_rig_path, source_mesh_path):
    retargeter = duplicate_or_load(BASE_NEW_RETARGETER_PATH, destination_path)
    source_rig = require_asset(source_rig_path)
    source_mesh = require_asset(source_mesh_path)
    target_rig = require_asset(NEW_IK_RIG_PATH)
    target_mesh = require_asset(NEW_MESH_PATH)

    controller = unreal.IKRetargeterController.get_controller(retargeter)
    source = unreal.RetargetSourceOrTarget.SOURCE
    target = unreal.RetargetSourceOrTarget.TARGET
    controller.set_ik_rig(source, source_rig)
    controller.set_preview_mesh(source, source_mesh)
    controller.assign_ik_rig_to_all_ops(source, source_rig)
    controller.set_ik_rig(target, target_rig)
    controller.set_preview_mesh(target, target_mesh)
    controller.assign_ik_rig_to_all_ops(target, target_rig)
    controller.auto_map_chains(unreal.AutoMapChainType.EXACT, True)
    unreal.EditorAssetLibrary.save_loaded_asset(retargeter, only_if_is_dirty=False)

    unreal.log_warning(
        f"[OriginalMaidMigration] configured={retargeter.get_path_name()} "
        f"source={source_mesh.get_path_name()} target={target_mesh.get_path_name()}"
    )
    return retargeter


def get_asset_data(paths):
    result = []
    for path in paths:
        asset = require_asset(path)
        data = unreal.EditorAssetLibrary.find_asset_data(asset.get_path_name())
        if not data or not data.is_valid():
            raise RuntimeError(f"Missing asset registry data: {path}")
        result.append(data)
    return result


def retarget_assets(label, paths, source_mesh_path, retargeter, include_references):
    target_mesh = require_asset(NEW_MESH_PATH)
    source_mesh = require_asset(source_mesh_path)
    asset_data = get_asset_data(paths)
    outputs = unreal.IKRetargetBatchOperation.duplicate_and_retarget(
        asset_data,
        source_mesh,
        target_mesh,
        retargeter,
        search="",
        replace="",
        prefix="",
        suffix="_OCM",
        include_referenced_assets=include_references,
        overwrite_existing_files=False,
    )
    unreal.log_warning(
        f"[OriginalMaidMigration] batch={label} input={len(asset_data)} "
        f"animation_outputs={len(outputs)}"
    )
    for output in outputs:
        asset = output.get_asset()
        if asset:
            unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False)
            unreal.log_warning(
                f"[OriginalMaidMigration] output={asset.get_path_name()}"
            )


def main():
    require_asset(BASE_NEW_RETARGETER_PATH)
    legacy_retargeter = configure_retargeter(
        LEGACY_RETARGETER_PATH, OLD_IK_RIG_PATH, OLD_MESH_PATH
    )
    mixamo_retargeter = configure_retargeter(
        MIXAMO_RETARGETER_PATH, MIXAMO_IK_RIG_PATH, MIXAMO_MESH_PATH
    )

    expected_legacy_anim_bp = "/Game/ABP_ArisNPC_OCM"
    if not unreal.EditorAssetLibrary.does_asset_exist(expected_legacy_anim_bp):
        retarget_assets(
            "legacy",
            LEGACY_ASSET_PATHS,
            OLD_MESH_PATH,
            legacy_retargeter,
            True,
        )
    else:
        unreal.log_warning(
            f"[OriginalMaidMigration] legacy batch skipped; found {expected_legacy_anim_bp}"
        )

    expected_mixamo_animation = "/Game/Angry_OCM"
    if not unreal.EditorAssetLibrary.does_asset_exist(expected_mixamo_animation):
        retarget_assets(
            "mixamo",
            MIXAMO_ASSET_PATHS,
            MIXAMO_MESH_PATH,
            mixamo_retargeter,
            False,
        )
    else:
        unreal.log_warning(
            f"[OriginalMaidMigration] Mixamo batch skipped; found {expected_mixamo_animation}"
        )

    if unreal.EditorAssetLibrary.does_asset_exist(expected_legacy_anim_bp):
        unreal.EditorAssetLibrary.save_asset(
            expected_legacy_anim_bp, only_if_is_dirty=False
        )
    unreal.EditorAssetLibrary.save_directory(ROOT, only_if_is_dirty=False, recursive=True)
    unreal.log_warning("[OriginalMaidMigration] ANIMATION_STAGE_COMPLETE")


try:
    main()
finally:
    unreal.SystemLibrary.quit_editor()
