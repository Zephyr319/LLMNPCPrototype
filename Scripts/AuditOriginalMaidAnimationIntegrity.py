import math
import unreal


ROOT = "/Game/Characters/OriginalCafeMaid/Animations"
ANIM_BLUEPRINT_PATH = f"{ROOT}/ABP_OriginalCafeMaid"
OLD_IDLE_PATH = f"{ROOT}/Anim_KA_Idle01_breathing_OCM"
FINAL_IDLE_PATH = f"{ROOT}/Anim_Idle_Neutral_OCM"
MIXAMO_ANIMATIONS = [
    "Anim_Angry_OCM",
    "Anim_Nervously_Look_Around_OCM",
    "Anim_PickUpDishes_OCM",
    "Anim_Reacting_OCM",
    "Anim_Searching_Files_High_OCM",
    "Anim_Standing_Arguing_OCM",
    "Anim_Standing_Using_Touchscreen_Tablet_OCM",
    "Anim_Surprised_OCM",
    "Anim_Telling_A_Secret_OCM",
    "Anim_Texting_OCM",
    "Anim_Texting_And_Walking_OCM",
    "Anim_Texting_While_Standing_OCM",
    "Anim_WaterPlant_OCM",
    "Anim_CleanCounter_OCM",
    "Anim_WipeDiningTable_OCM",
    "Anim_Yawn_OCM",
]
SEAT_ANIMATIONS = [
    "Anim_KA_Sit_Start_OCM",
    "Anim_KA_Sit_LookingAround_OCM",
    "Anim_KA_Sit_CrossLegs_OCM",
    "Anim_KA_Sit_End_OCM",
]
SAMPLE_BONES = [
    "J_Bip_C_Hips",
    "J_Bip_C_Head",
    "J_Bip_L_Hand",
    "J_Bip_R_Hand",
    "J_Bip_L_Foot",
    "J_Bip_R_Foot",
]


def require_asset(path):
    asset = unreal.load_asset(path)
    if not asset:
        raise RuntimeError(f"Missing asset: {path}")
    return asset


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
    for value in (registry.get_dependencies(ANIM_BLUEPRINT_PATH, options) or [])
}
if FINAL_IDLE_PATH not in dependencies:
    raise RuntimeError("Animation blueprint does not reference the corrected idle")
if OLD_IDLE_PATH in dependencies:
    raise RuntimeError("Animation blueprint still references the uncorrected idle")

idle = require_asset(FINAL_IDLE_PATH)
pose_options = unreal.AnimPoseEvaluationOptions()
idle_min_clearance = 100000.0
idle_max_shoulder_drop = -100000.0
for fraction in (0.0, 0.25, 0.5, 0.75):
    pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(
        idle, idle.get_play_length() * fraction, pose_options
    )
    left = unreal.AnimPoseExtensions.get_bone_pose(
        pose, "J_Bip_L_Hand", unreal.AnimPoseSpaces.WORLD
    ).translation
    right = unreal.AnimPoseExtensions.get_bone_pose(
        pose, "J_Bip_R_Hand", unreal.AnimPoseSpaces.WORLD
    ).translation
    left_shoulder = unreal.AnimPoseExtensions.get_bone_pose(
        pose, "J_Bip_L_Shoulder", unreal.AnimPoseSpaces.WORLD
    ).translation
    left_upper_arm = unreal.AnimPoseExtensions.get_bone_pose(
        pose, "J_Bip_L_UpperArm", unreal.AnimPoseSpaces.WORLD
    ).translation
    right_shoulder = unreal.AnimPoseExtensions.get_bone_pose(
        pose, "J_Bip_R_Shoulder", unreal.AnimPoseSpaces.WORLD
    ).translation
    right_upper_arm = unreal.AnimPoseExtensions.get_bone_pose(
        pose, "J_Bip_R_UpperArm", unreal.AnimPoseSpaces.WORLD
    ).translation
    idle_min_clearance = min(idle_min_clearance, left.x, -right.x)
    idle_max_shoulder_drop = max(
        idle_max_shoulder_drop,
        left_shoulder.z - left_upper_arm.z,
        right_shoulder.z - right_upper_arm.z,
    )
if idle_min_clearance < 20.0:
    raise RuntimeError(
        f"Corrected idle arm clearance is too small: {idle_min_clearance:.3f}"
    )
if idle_max_shoulder_drop > 2.0:
    raise RuntimeError(
        "Corrected idle shoulders still slope too far downward: "
        f"{idle_max_shoulder_drop:.3f}"
    )
unreal.log_warning(
    f"[MaidAnimationIntegrity] idle=PASS "
    f"min_hand_clearance={idle_min_clearance:.3f} "
    f"max_shoulder_drop={idle_max_shoulder_drop:.3f}"
)

for name in MIXAMO_ANIMATIONS + SEAT_ANIMATIONS:
    animation = require_asset(f"{ROOT}/{name}")
    length = animation.get_play_length()
    if length <= 0.05:
        raise RuntimeError(f"Animation has invalid length: {name} -> {length}")
    for fraction in (0.0, 0.5, 0.9):
        pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(
            animation, length * fraction, pose_options
        )
        for bone_name in SAMPLE_BONES:
            vector = unreal.AnimPoseExtensions.get_bone_pose(
                pose, bone_name, unreal.AnimPoseSpaces.WORLD
            ).translation
            values = (vector.x, vector.y, vector.z)
            if not all(math.isfinite(value) for value in values):
                raise RuntimeError(
                    f"Non-finite pose: {name} {bone_name} {fraction}"
                )
            if max(abs(value) for value in values) > 500.0:
                raise RuntimeError(
                    f"Out-of-range pose: {name} {bone_name} {values}"
                )
    unreal.log_warning(
        f"[MaidAnimationIntegrity] animation=PASS name={name} length={length:.3f}"
    )

unreal.log_warning("[MaidAnimationIntegrity] ALL_CHECKS_PASS")
