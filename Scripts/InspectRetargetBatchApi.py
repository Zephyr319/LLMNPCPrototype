import unreal


unreal.log_warning(
    "[RetargetBatchApi] duplicate_and_retarget="
    + str(unreal.IKRetargetBatchOperation.duplicate_and_retarget.__doc__)
)
unreal.log_warning(
    "[RetargetBatchApi] auto_map_chains="
    + str(unreal.IKRetargeterController.auto_map_chains.__doc__)
)
unreal.log_warning(
    "[RetargetBatchApi] set_ik_rig="
    + str(unreal.IKRetargeterController.set_ik_rig.__doc__)
)
unreal.log_warning("[RetargetBatchApi] COMPLETE")
