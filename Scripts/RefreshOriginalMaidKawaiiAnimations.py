import unreal


SOURCE_ROOT = "/Game/KawaiiAnimations/Animations"
TARGET_ROOT = "/Game/Characters/OriginalCafeMaid/Animations"
SOURCE_MESH_PATH = "/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple"
TARGET_MESH_PATH = (
    "/Game/Characters/OriginalCafeMaid/SK_OriginalCafeMaid_v1_0"
)
RETARGETER_PATH = (
    "/Game/Characters/OriginalCafeMaid/RTG_OriginalCafeMaid_v1_0"
)
FINAL_IDLE_PATH = f"{TARGET_ROOT}/Anim_Idle_Neutral_OCM"
GENERATED_SUFFIX = "_OCM_DIRECT_REFRESH"
IDLE_ARM_OFFSET_DEGREES = 8.0
ANIMATION_NAMES = (
    "Anim_KA_Idle01_breathing",
    "Anim_KA_Idle02_LookLeftAndRight",
    "Anim_KA_Idle03_LookAtHands",
    "Anim_KA_Idle04_LookAtFeet",
    "Anim_KA_Idle06_JumpAround",
    "Anim_KA_Idle08_ComeUpWithAnIdea",
    "Anim_KA_Idle12_LeaningForward",
    "Anim_KA_Sit_Start",
    "Anim_KA_Sit_LookingAround",
    "Anim_KA_Sit_CrossLegs",
    "Anim_KA_Sit_End",
    "Anim_KA_Run01",
    "Anim_KA_Walk03",
    "Anim_KA_Walk04",
)


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


def find_generated_assets():
    return [
        path
        for path in unreal.EditorAssetLibrary.list_assets(
            "/Game", recursive=True, include_folder=False
        )
        if GENERATED_SUFFIX in path
    ]


def delete_generated_assets():
    for path in find_generated_assets():
        if unreal.EditorAssetLibrary.does_asset_exist(path):
            if not unreal.EditorAssetLibrary.delete_asset(path):
                raise RuntimeError(f"Could not delete generated asset: {path}")


def copy_animation_data(source_animation, destination_path):
    destination_animation = unreal.load_asset(destination_path)
    if not destination_animation:
        source_path = source_animation.get_path_name()
        if not unreal.EditorAssetLibrary.rename_asset(
            source_path, destination_path
        ):
            raise RuntimeError(
                f"Could not create animation asset: {destination_path}"
            )
        destination_animation = require_asset(destination_path)
        unreal.EditorAssetLibrary.save_loaded_asset(
            destination_animation, only_if_is_dirty=False
        )
        return

    copied = (
        unreal.AssetReferenceReplacementLibrary.copy_animation_sequence_data(
            destination_animation, source_animation
        )
    )
    if not copied:
        raise RuntimeError(
            f"Could not update animation data: {destination_path}"
        )
    unreal.EditorAssetLibrary.save_loaded_asset(
        destination_animation, only_if_is_dirty=False
    )


delete_generated_assets()

source_mesh = require_asset(SOURCE_MESH_PATH)
target_mesh = require_asset(TARGET_MESH_PATH)
retargeter = require_asset(RETARGETER_PATH)
source_assets = [
    require_asset(f"{SOURCE_ROOT}/{name}") for name in ANIMATION_NAMES
]
asset_data = [
    unreal.EditorAssetLibrary.find_asset_data(asset.get_path_name())
    for asset in source_assets
]

outputs = unreal.IKRetargetBatchOperation.duplicate_and_retarget(
    asset_data,
    source_mesh,
    target_mesh,
    retargeter,
    search="",
    replace="",
    prefix="",
    suffix=GENERATED_SUFFIX,
    include_referenced_assets=False,
    overwrite_existing_files=False,
)
if len(outputs) != len(ANIMATION_NAMES):
    raise RuntimeError(
        f"Expected {len(ANIMATION_NAMES)} outputs, got {len(outputs)}"
    )

updated_names = set()
generated_idle = None
for output in outputs:
    generated_animation = output.get_asset()
    if not generated_animation:
        raise RuntimeError("Retarget operation returned an empty output")

    generated_name = generated_animation.get_name()
    if not generated_name.endswith(GENERATED_SUFFIX):
        raise RuntimeError(f"Unexpected generated name: {generated_name}")

    source_name = generated_name[: -len(GENERATED_SUFFIX)]
    if source_name not in ANIMATION_NAMES:
        raise RuntimeError(f"Unexpected generated animation: {source_name}")

    destination_path = f"{TARGET_ROOT}/{source_name}_OCM"
    copy_animation_data(generated_animation, destination_path)
    if source_name == "Anim_KA_Idle01_breathing":
        generated_idle = generated_animation
    updated_names.add(source_name)
    unreal.log_warning(
        f"[KawaiiDirectRefresh] updated={destination_path}"
    )

if updated_names != set(ANIMATION_NAMES):
    missing = sorted(set(ANIMATION_NAMES) - updated_names)
    raise RuntimeError("Animations were not updated: " + ", ".join(missing))

if not generated_idle:
    raise RuntimeError("The directly retargeted idle animation was not generated")

left_ok = unreal.AssetReferenceReplacementLibrary.apply_bone_rotation_offset(
    generated_idle,
    "J_Bip_L_UpperArm",
    unreal.Rotator(
        pitch=IDLE_ARM_OFFSET_DEGREES, yaw=0.0, roll=0.0
    ),
    True,
)
right_ok = unreal.AssetReferenceReplacementLibrary.apply_bone_rotation_offset(
    generated_idle,
    "J_Bip_R_UpperArm",
    unreal.Rotator(
        pitch=-IDLE_ARM_OFFSET_DEGREES, yaw=0.0, roll=0.0
    ),
    True,
)
if not left_ok or not right_ok:
    raise RuntimeError("Could not apply the final idle arm correction")

copy_animation_data(generated_idle, FINAL_IDLE_PATH)
unreal.log_warning(
    f"[KawaiiDirectRefresh] corrected_idle={FINAL_IDLE_PATH} "
    f"arm_offset={IDLE_ARM_OFFSET_DEGREES:.1f}"
)

delete_generated_assets()
unreal.log_warning(
    f"[KawaiiDirectRefresh] COMPLETE count={len(updated_names)}"
)
schedule_editor_quit()
