import unreal


def matching_names(value, terms):
    names = []
    for name in dir(value):
        lowered = name.lower()
        if any(term in lowered for term in terms):
            names.append(name)
    return ",".join(sorted(names))


mesh = unreal.load_asset(
    "/Game/Characters/OriginalCafeMaid/SK_OriginalCafeMaid_v1_0"
)
skeleton = mesh.get_editor_property("skeleton")
retargeter = unreal.load_asset(
    "/Game/Characters/OriginalCafeMaid/RTG_OriginalCafeMaid_v1_0"
)

unreal.log_warning(
    "[AnimationPythonApi] mesh="
    + matching_names(mesh, ["bone", "morph", "skeleton", "socket"])
)
unreal.log_warning(
    "[AnimationPythonApi] skeleton="
    + matching_names(skeleton, ["bone", "curve", "socket"])
)
unreal.log_warning(
    "[AnimationPythonApi] skeletal_mesh_editor_subsystem="
    + matching_names(unreal.SkeletalMeshEditorSubsystem, ["bone", "morph", "skeleton"])
)
unreal.log_warning(
    "[AnimationPythonApi] animation_library="
    + matching_names(unreal.AnimationLibrary, ["bone", "curve", "skeleton"])
)
unreal.log_warning(
    "[AnimationPythonApi] ik_retargeter_controller="
    + matching_names(unreal.IKRetargeterController, ["rig", "mesh", "chain", "retarget"])
)
unreal.log_warning(
    "[AnimationPythonApi] retargeter="
    + matching_names(retargeter, ["rig", "mesh", "chain", "retarget"])
)
unreal.log_warning("[AnimationPythonApi] COMPLETE")
