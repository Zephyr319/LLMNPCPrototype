import unreal


ROOT = "/Game/Characters/OriginalCafeMaid/Animations"
ANIMATIONS = [
    "Anim_KA_Idle01_breathing_OCM",
    "Anim_KA_Idle02_LookLeftAndRight_OCM",
    "Anim_KA_Idle03_LookAtHands_OCM",
    "Anim_KA_Idle04_LookAtFeet_OCM",
    "Anim_KA_Idle06_JumpAround_OCM",
    "Anim_KA_Idle08_ComeUpWithAnIdea_OCM",
    "Anim_KA_Idle12_LeaningForward_OCM",
    "Anim_Nervously_Look_Around_OCM",
    "Anim_Texting_While_Standing_OCM",
    "Anim_Yawn_OCM",
]

BONES = [
    "J_Bip_C_UpperChest",
    "J_Bip_L_Shoulder",
    "J_Bip_L_LowerArm",
    "J_Bip_L_Hand",
    "J_Bip_R_Shoulder",
    "J_Bip_R_LowerArm",
    "J_Bip_R_Hand",
]


def vector_text(vector):
    return f"({vector.x:.2f},{vector.y:.2f},{vector.z:.2f})"


options = unreal.AnimPoseEvaluationOptions()
for name in ANIMATIONS:
    path = f"{ROOT}/{name}"
    animation = unreal.load_asset(path)
    if not animation:
        raise RuntimeError(f"Missing animation: {path}")
    length = animation.get_play_length()
    for fraction in (0.0, 0.25, 0.5, 0.75):
        pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(
            animation, length * fraction, options
        )
        values = []
        for bone_name in BONES:
            transform = unreal.AnimPoseExtensions.get_bone_pose(
                pose, bone_name, unreal.AnimPoseSpaces.WORLD
            )
            values.append(f"{bone_name}={vector_text(transform.translation)}")
        unreal.log_warning(
            f"[MaidClearanceAudit] animation={name} fraction={fraction:.2f} "
            + " ; ".join(values)
        )

unreal.log_warning("[MaidClearanceAudit] COMPLETE")
