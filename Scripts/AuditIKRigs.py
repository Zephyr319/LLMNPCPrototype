import unreal


IK_RIGS = [
    "/Game/art/虚拟女仆/IK_（25_9_9更新）midars_Mannequin",
    "/Game/art/actions/IK_midars_MixamoTarget",
    "/Game/Characters/OriginalCafeMaid/IK_OriginalCafeMaid_v1_0_Mannequin",
]


for path in IK_RIGS:
    rig = unreal.load_asset(path)
    if not rig:
        raise RuntimeError(f"Missing IK rig: {path}")
    controller = unreal.IKRigController.get_controller(rig)
    chains = []
    for chain in controller.get_retarget_chains():
        chain_name = chain.chain_name
        chains.append(
            f"{chain_name}:{controller.get_retarget_chain_start_bone(chain_name)}"
            f"->{controller.get_retarget_chain_end_bone(chain_name)}"
        )
    mesh = controller.get_skeletal_mesh()
    unreal.log_warning(
        f"[IKRigAudit] rig={rig.get_path_name()} "
        f"mesh={mesh.get_path_name() if mesh else 'None'} "
        f"root={controller.get_retarget_root()} chains={';'.join(chains)}"
    )

unreal.log_warning("[IKRigAudit] COMPLETE")
