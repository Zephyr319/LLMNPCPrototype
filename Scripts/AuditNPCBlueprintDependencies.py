import unreal


ROOT_PACKAGE = "/Game/Blueprints/BP_LLM_NPC_VRM"

options = unreal.AssetRegistryDependencyOptions()
for property_name in (
    "include_editor_only_package_references",
    "include_game_package_references",
    "include_hard_package_references",
    "include_soft_package_references",
    "include_hard_management_references",
    "include_soft_management_references",
):
    options.set_editor_property(property_name, True)
options.set_editor_property("include_searchable_names", False)

registry = unreal.AssetRegistryHelpers.get_asset_registry()
dependencies = registry.get_dependencies(ROOT_PACKAGE, options) or []
unreal.log_warning(
    f"[NPCDependencyAudit] package={ROOT_PACKAGE} direct_count={len(dependencies)}"
)
for dependency in sorted(str(value) for value in dependencies):
    unreal.log_warning(f"[NPCDependencyAudit] dependency={dependency}")

unreal.log_warning("[NPCDependencyAudit] COMPLETE")
