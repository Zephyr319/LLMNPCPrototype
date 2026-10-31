import unreal


RETARGETERS = [
    "/Game/art/虚拟女仆/RTG_（25_9_9更新）midars",
    "/Game/art/actions/RTG_Mixamo_To_midars",
    "/Game/Characters/OriginalCafeMaid/RTG_OriginalCafeMaid_v1_0",
    "/Game/Characters/OriginalCafeMaid/RTG_LegacyMaid_To_OriginalCafeMaid",
    "/Game/Characters/OriginalCafeMaid/RTG_Mixamo_To_OriginalCafeMaid",
]


def describe_pose(controller, side):
    pose_name = controller.get_current_retarget_pose_name(side)
    pose = controller.get_current_retarget_pose(side)
    offsets = pose.get_editor_property("bone_rotation_offsets")
    root = pose.get_editor_property("root_translation_offset")
    serialized = []
    for bone_name, quat in offsets.items():
        rotator = quat.rotator()
        serialized.append(
            f"{bone_name}=P{rotator.pitch:.3f},Y{rotator.yaw:.3f},R{rotator.roll:.3f}"
        )
    serialized.sort()
    return pose_name, root, serialized


for path in RETARGETERS:
    asset = unreal.load_asset(path)
    if not asset:
        raise RuntimeError(f"Missing retargeter: {path}")
    controller = unreal.IKRetargeterController.get_controller(asset)
    unreal.log_warning(f"[RetargetPoseAudit] asset={path}")
    for side in (
        unreal.RetargetSourceOrTarget.SOURCE,
        unreal.RetargetSourceOrTarget.TARGET,
    ):
        label = "source" if side == unreal.RetargetSourceOrTarget.SOURCE else "target"
        pose_name, root, offsets = describe_pose(controller, side)
        unreal.log_warning(
            f"[RetargetPoseAudit] {label} pose={pose_name} root={root} "
            f"offset_count={len(offsets)} offsets={' ; '.join(offsets)}"
        )

unreal.log_warning("[RetargetPoseAudit] COMPLETE")
