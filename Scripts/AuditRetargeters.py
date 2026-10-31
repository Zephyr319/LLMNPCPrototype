import unreal


RETARGETERS = [
    "/Game/art/虚拟女仆/RTG_（25_9_9更新）midars",
    "/Game/art/actions/RTG_Mixamo_To_midars",
    "/Game/Characters/OriginalCafeMaid/RTG_OriginalCafeMaid_v1_0",
]


def asset_path(asset):
    return asset.get_path_name() if asset else "None"


def audit(path):
    asset = unreal.load_asset(path)
    if not asset:
        raise RuntimeError(f"Missing retargeter: {path}")

    controller = unreal.IKRetargeterController.get_controller(asset)
    source = unreal.RetargetSourceOrTarget.SOURCE
    target = unreal.RetargetSourceOrTarget.TARGET
    unreal.log_warning(
        f"[RetargeterAudit] asset={asset_path(asset)} "
        f"source_rig={asset_path(controller.get_ik_rig(source))} "
        f"source_mesh={asset_path(controller.get_preview_mesh(source))} "
        f"target_rig={asset_path(controller.get_ik_rig(target))} "
        f"target_mesh={asset_path(controller.get_preview_mesh(target))}"
    )


for retargeter_path in RETARGETERS:
    audit(retargeter_path)

unreal.log_warning("[RetargeterAudit] COMPLETE")
