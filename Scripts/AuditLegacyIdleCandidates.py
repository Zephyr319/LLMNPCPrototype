import unreal


ROOT = "/Game/art/虚拟女仆"
ANIMATIONS = [
    "Anim_KA_Idle01_breathing",
    "Anim_KA_Idle02_LookLeftAndRight",
    "Anim_KA_Idle03_LookAtHands",
    "Anim_KA_Idle04_LookAtFeet",
    "Anim_KA_Idle05_Stretch",
    "Anim_KA_Idle06_JumpAround",
    "Anim_KA_Idle07_SpinningJump",
    "Anim_KA_Idle08_ComeUpWithAnIdea",
    "Anim_KA_Idle09_Waiting",
    "Anim_KA_Idle11_LookingBack",
    "Anim_KA_Idle12_LeaningForward",
    "Anim_KA_Idle13_Dance01",
    "Anim_KA_Idle14_Dance02",
]

BONES = ["chest", "shoulder_L", "hand_L", "shoulder_R", "hand_R"]


def vector_text(vector):
    return f"({vector.x:.2f},{vector.y:.2f},{vector.z:.2f})"


options = unreal.AnimPoseEvaluationOptions()
for name in ANIMATIONS:
    animation = unreal.load_asset(f"{ROOT}/{name}")
    if not animation:
        unreal.log_warning(f"[LegacyIdleAudit] missing={name}")
        continue
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
            f"[LegacyIdleAudit] animation={name} fraction={fraction:.2f} "
            + " ; ".join(values)
        )

unreal.log_warning("[LegacyIdleAudit] COMPLETE")
