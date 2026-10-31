import unreal


ROOT = "/Game/Characters/OriginalCafeMaid"
ANIMATION_ROOT = f"{ROOT}/Animations"
BASE_RETARGETER_PATH = f"{ROOT}/RTG_OriginalCafeMaid_v1_0"
RETARGETER_PATH = f"{ROOT}/RTG_ActorCoreFemale_To_OriginalCafeMaid"
SOURCE_RIG_PATH = "/Game/ActorCore_Sample_Motions/Dummy_Female/Dummy_Female_IKRig"
SOURCE_MESH_PATH = "/Game/ActorCore_Sample_Motions/Dummy_Female/Dummy_Female"
TARGET_RIG_PATH = f"{ROOT}/IK_OriginalCafeMaid_v1_0_Mannequin"
TARGET_MESH_PATH = f"{ROOT}/SK_OriginalCafeMaid_v1_0"
SOURCE_ANIMATION_PATH = (
    "/Game/ActorCore_Sample_Motions/Idle_walk_sit_Female_Motion/"
    "Female_Sit_talk_Anim"
)
FINAL_ANIMATION_PATH = f"{ANIMATION_ROOT}/Anim_Female_Sit_Talk_OCM"
TEMP_SUFFIX = "_OCM_SEATED_TEMP"


def require_asset(path):
    asset = unreal.load_asset(path)
    if not asset:
        raise RuntimeError(f"Missing required asset: {path}")
    return asset


def schedule_editor_quit(delay_seconds=3.0):
    state = {"elapsed": 0.0, "handle": None}

    def tick(delta_seconds):
        state["elapsed"] += delta_seconds
        if state["elapsed"] < delay_seconds:
            return
        unreal.unregister_slate_post_tick_callback(state["handle"])
        unreal.SystemLibrary.quit_editor()

    state["handle"] = unreal.register_slate_post_tick_callback(tick)


def configure_retargeter():
    retargeter = unreal.load_asset(RETARGETER_PATH)
    if not retargeter:
        if not unreal.EditorAssetLibrary.duplicate_asset(
            BASE_RETARGETER_PATH, RETARGETER_PATH
        ):
            raise RuntimeError("Could not create ActorCore seated retargeter")
        retargeter = require_asset(RETARGETER_PATH)

    controller = unreal.IKRetargeterController.get_controller(retargeter)
    source = unreal.RetargetSourceOrTarget.SOURCE
    target = unreal.RetargetSourceOrTarget.TARGET
    source_rig = require_asset(SOURCE_RIG_PATH)
    source_mesh = require_asset(SOURCE_MESH_PATH)
    target_rig = require_asset(TARGET_RIG_PATH)
    target_mesh = require_asset(TARGET_MESH_PATH)

    controller.set_ik_rig(source, source_rig)
    controller.set_preview_mesh(source, source_mesh)
    controller.assign_ik_rig_to_all_ops(source, source_rig)
    controller.set_ik_rig(target, target_rig)
    controller.set_preview_mesh(target, target_mesh)
    controller.assign_ik_rig_to_all_ops(target, target_rig)
    controller.auto_map_chains(unreal.AutoMapChainType.EXACT, True)
    unreal.EditorAssetLibrary.save_loaded_asset(retargeter, only_if_is_dirty=False)
    return retargeter


def main():
    if unreal.EditorAssetLibrary.does_asset_exist(FINAL_ANIMATION_PATH):
        unreal.log_warning(
            f"[SeatedConversationRetarget] already exists={FINAL_ANIMATION_PATH}"
        )
        return

    retargeter = configure_retargeter()
    source_mesh = require_asset(SOURCE_MESH_PATH)
    target_mesh = require_asset(TARGET_MESH_PATH)
    source_animation = require_asset(SOURCE_ANIMATION_PATH)
    asset_data = unreal.EditorAssetLibrary.find_asset_data(
        source_animation.get_path_name()
    )
    outputs = unreal.IKRetargetBatchOperation.duplicate_and_retarget(
        [asset_data],
        source_mesh,
        target_mesh,
        retargeter,
        search="",
        replace="",
        prefix="",
        suffix=TEMP_SUFFIX,
        include_referenced_assets=False,
        overwrite_existing_files=False,
    )
    if len(outputs) != 1 or not outputs[0].get_asset():
        raise RuntimeError("ActorCore seated conversation retarget failed")

    generated = outputs[0].get_asset()
    generated_path = generated.get_path_name().split(".", 1)[0]
    if not unreal.EditorAssetLibrary.rename_asset(
        generated_path, FINAL_ANIMATION_PATH
    ):
        raise RuntimeError(
            f"Could not rename {generated_path} to {FINAL_ANIMATION_PATH}"
        )
    final_animation = require_asset(FINAL_ANIMATION_PATH)
    unreal.EditorAssetLibrary.save_loaded_asset(
        final_animation, only_if_is_dirty=False
    )
    unreal.EditorAssetLibrary.save_loaded_asset(
        retargeter, only_if_is_dirty=False
    )
    unreal.log_warning(
        f"[SeatedConversationRetarget] COMPLETE output={FINAL_ANIMATION_PATH}"
    )


try:
    main()
finally:
    schedule_editor_quit()
