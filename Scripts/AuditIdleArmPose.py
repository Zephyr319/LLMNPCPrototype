import unreal


ANIMATIONS = [
    (
        "original_mannequin",
        "/Game/KawaiiAnimations/Animations/Anim_KA_Idle01_breathing",
        [
            "pelvis",
            "spine_03",
            "clavicle_l",
            "upperarm_l",
            "lowerarm_l",
            "hand_l",
            "clavicle_r",
            "upperarm_r",
            "lowerarm_r",
            "hand_r",
        ],
    ),
    (
        "legacy_vrm",
        "/Game/art/虚拟女仆/Anim_KA_Idle01_breathing",
        [
            "hips",
            "chest",
            "shoulder_L",
            "upper_arm_L",
            "lower_arm_L",
            "hand_L",
            "shoulder_R",
            "upper_arm_R",
            "lower_arm_R",
            "hand_R",
        ],
    ),
    (
        "original_cafe_maid",
        "/Game/Characters/OriginalCafeMaid/Animations/Anim_KA_Idle01_breathing_OCM",
        [
            "J_Bip_C_Hips",
            "J_Bip_C_UpperChest",
            "J_Bip_L_Shoulder",
            "J_Bip_L_UpperArm",
            "J_Bip_L_LowerArm",
            "J_Bip_L_Hand",
            "J_Bip_R_Shoulder",
            "J_Bip_R_UpperArm",
            "J_Bip_R_LowerArm",
            "J_Bip_R_Hand",
        ],
    ),
]


def vector_text(vector):
    return f"({vector.x:.3f},{vector.y:.3f},{vector.z:.3f})"


options = unreal.AnimPoseEvaluationOptions()
for label, path, bone_names in ANIMATIONS:
    animation = unreal.load_asset(path)
    if not animation:
        raise RuntimeError(f"Missing animation: {path}")
    play_length = animation.get_play_length()
    unreal.log_warning(
        f"[IdleArmPoseAudit] animation={label} path={path} length={play_length:.3f}"
    )
    for fraction in (0.0, 0.25, 0.5, 0.75):
        time = play_length * fraction
        pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(
            animation, time, options
        )
        values = []
        for bone_name in bone_names:
            transform = unreal.AnimPoseExtensions.get_bone_pose(
                pose, bone_name, unreal.AnimPoseSpaces.WORLD
            )
            values.append(f"{bone_name}={vector_text(transform.translation)}")
        unreal.log_warning(
            f"[IdleArmPoseAudit] animation={label} fraction={fraction:.2f} "
            + " ; ".join(values)
        )

unreal.log_warning("[IdleArmPoseAudit] COMPLETE")
