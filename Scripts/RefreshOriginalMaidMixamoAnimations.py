import unreal


ROOT = "/Game/Characters/OriginalCafeMaid"
ANIMATION_ROOT = f"{ROOT}/Animations"
LEGACY_RETARGETER_PATH = f"{ROOT}/RTG_LegacyMaid_To_OriginalCafeMaid"
MIXAMO_RETARGETER_PATH = f"{ROOT}/RTG_Mixamo_To_OriginalCafeMaid"
SOURCE_MESH_PATH = "/Game/art/actions/Watering"
TARGET_MESH_PATH = f"{ROOT}/SK_OriginalCafeMaid_v1_0"
TEMP_SUFFIX = "_OCM_REFRESH"

SOURCE_TO_FINAL = {
    "/Game/art/actions/Angry": "Anim_Angry_OCM",
    "/Game/art/actions/Nervously_Look_Around": "Anim_Nervously_Look_Around_OCM",
    "/Game/art/actions/Pick_up": "Anim_PickUpDishes_OCM",
    "/Game/art/actions/Reacting": "Anim_Reacting_OCM",
    "/Game/art/actions/Searching_Files_High": "Anim_Searching_Files_High_OCM",
    "/Game/art/actions/Standing_Arguing_Anim": "Anim_Standing_Arguing_OCM",
    "/Game/art/actions/Standing_Using_Touchscreen_Tablet": (
        "Anim_Standing_Using_Touchscreen_Tablet_OCM"
    ),
    "/Game/art/actions/Surprised": "Anim_Surprised_OCM",
    "/Game/art/actions/Telling_A_Secret": "Anim_Telling_A_Secret_OCM",
    "/Game/art/actions/Texting": "Anim_Texting_OCM",
    "/Game/art/actions/Texting_And_Walking": "Anim_Texting_And_Walking_OCM",
    "/Game/art/actions/Texting_While_Standing": "Anim_Texting_While_Standing_OCM",
    "/Game/art/actions/Watering_Anim": "Anim_WaterPlant_OCM",
    "/Game/art/actions/pick_up_table": "Anim_CleanCounter_OCM",
    "/Game/art/actions/pick_up_table__2_": "Anim_WipeDiningTable_OCM",
    "/Game/art/actions/Yawn": "Anim_Yawn_OCM",
}


def require_asset(path):
    asset = unreal.load_asset(path)
    if not asset:
        raise RuntimeError(f"Missing asset: {path}")
    return asset


def reset_pose(controller, side):
    pose_name = controller.get_current_retarget_pose_name(side)
    controller.reset_retarget_pose(pose_name, [], side)


legacy_retargeter = require_asset(LEGACY_RETARGETER_PATH)
legacy_controller = unreal.IKRetargeterController.get_controller(legacy_retargeter)
reset_pose(legacy_controller, unreal.RetargetSourceOrTarget.SOURCE)
reset_pose(legacy_controller, unreal.RetargetSourceOrTarget.TARGET)
unreal.EditorAssetLibrary.save_loaded_asset(
    legacy_retargeter, only_if_is_dirty=False
)

mixamo_retargeter = require_asset(MIXAMO_RETARGETER_PATH)
mixamo_controller = unreal.IKRetargeterController.get_controller(mixamo_retargeter)
reset_pose(mixamo_controller, unreal.RetargetSourceOrTarget.SOURCE)
reset_pose(mixamo_controller, unreal.RetargetSourceOrTarget.TARGET)
mixamo_controller.auto_align_all_bones(
    unreal.RetargetSourceOrTarget.TARGET,
    unreal.RetargetAutoAlignMethod.CHAIN_TO_CHAIN,
)
unreal.EditorAssetLibrary.save_loaded_asset(
    mixamo_retargeter, only_if_is_dirty=False
)

source_mesh = require_asset(SOURCE_MESH_PATH)
target_mesh = require_asset(TARGET_MESH_PATH)
source_assets = [require_asset(path) for path in SOURCE_TO_FINAL]
asset_data = [
    unreal.EditorAssetLibrary.find_asset_data(asset.get_path_name())
    for asset in source_assets
]
if any(not data or not data.is_valid() for data in asset_data):
    raise RuntimeError("One or more Mixamo source assets have invalid registry data")

outputs = unreal.IKRetargetBatchOperation.duplicate_and_retarget(
    asset_data,
    source_mesh,
    target_mesh,
    mixamo_retargeter,
    search="",
    replace="",
    prefix="",
    suffix=TEMP_SUFFIX,
    include_referenced_assets=False,
    overwrite_existing_files=True,
)

outputs_by_source_name = {}
for output in outputs:
    animation = output.get_asset()
    if animation:
        output_name = animation.get_name()
        expected_suffix = TEMP_SUFFIX
        if not output_name.endswith(expected_suffix):
            raise RuntimeError(f"Unexpected generated animation name: {output_name}")
        source_name = output_name[: -len(expected_suffix)]
        outputs_by_source_name[source_name] = animation

updated = 0
temporary_paths = []
for source_path, final_name in SOURCE_TO_FINAL.items():
    source_name = source_path.rsplit("/", 1)[-1]
    generated = outputs_by_source_name.get(source_name)
    destination = require_asset(f"{ANIMATION_ROOT}/{final_name}")
    if not generated:
        raise RuntimeError(f"Missing generated animation for: {source_name}")
    if not unreal.AssetReferenceReplacementLibrary.copy_animation_sequence_data(
        destination, generated
    ):
        raise RuntimeError(f"Could not update final animation: {final_name}")
    unreal.EditorAssetLibrary.save_loaded_asset(destination, only_if_is_dirty=False)
    temporary_paths.append(generated.get_path_name().split(".", 1)[0])
    updated += 1
    unreal.log_warning(
        f"[MixamoRefresh] updated={destination.get_path_name()} "
        f"source={generated.get_path_name()}"
    )

for path in temporary_paths:
    unreal.EditorAssetLibrary.delete_asset(path)

unreal.log_warning(
    f"[MixamoRefresh] COMPLETE updated={updated} generated={len(outputs)}"
)

# The script runs during editor startup; defer shutdown until the level editor
# has completed its own initialization to avoid UE 5.7 teardown assertions.
_exit_delay = 0.0
_exit_handle = None


def _exit_when_ready(delta_seconds):
    global _exit_delay, _exit_handle
    _exit_delay += delta_seconds
    if _exit_delay >= 3.0:
        unreal.unregister_slate_post_tick_callback(_exit_handle)
        unreal.SystemLibrary.quit_editor()


_exit_handle = unreal.register_slate_post_tick_callback(_exit_when_ready)
