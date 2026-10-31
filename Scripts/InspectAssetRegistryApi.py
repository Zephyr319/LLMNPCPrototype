import unreal


unreal.log_warning(
    "[AssetRegistryApi] options="
    + str(unreal.AssetRegistryDependencyOptions.__doc__)
)
unreal.log_warning(
    "[AssetRegistryApi] get_dependencies="
    + str(unreal.AssetRegistry.get_dependencies.__doc__)
)
unreal.log_warning("[AssetRegistryApi] COMPLETE")
