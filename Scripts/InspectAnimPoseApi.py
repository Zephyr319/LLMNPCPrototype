import unreal


TARGETS = [
    unreal.AnimPoseExtensions.get_anim_pose_at_time,
    unreal.AnimPoseExtensions.get_bone_pose,
]

for target in TARGETS:
    unreal.log_warning(f"[AnimPoseApi] {target.__name__}: {target.__doc__}")

unreal.log_warning(f"[AnimPoseApi] spaces={list(unreal.AnimPoseSpaces)}")
unreal.log_warning("[AnimPoseApi] COMPLETE")
