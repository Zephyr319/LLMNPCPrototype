import unreal


NPC_BLUEPRINT_PATH = "/Game/Blueprints/BP_LLM_NPC_VRM"
BACKUP_PATH = "/Game/Blueprints/Backup/BP_LLM_NPC_VRM_PreOriginalMaid"
ANIMATION_ROOT = "/Game/Characters/OriginalCafeMaid/Animations"
OLD_MONTAGE_PATHS = [
    "/Game/art/虚拟女仆/TM_Explain",
    "/Game/art/虚拟女仆/TM_Greet",
    "/Game/art/虚拟女仆/TM_Recommend",
    "/Game/art/虚拟女仆/TM_serve_food",
]
NEW_MONTAGE_PATHS = [
    f"{ANIMATION_ROOT}/AM_Explain",
    f"{ANIMATION_ROOT}/AM_Greet",
    f"{ANIMATION_ROOT}/AM_Recommend",
    f"{ANIMATION_ROOT}/AM_ServeFood",
]


def require_asset(path):
    asset = unreal.load_asset(path)
    if not asset:
        raise RuntimeError(f"Missing required asset: {path}")
    return asset


def main():
    blueprint = require_asset(NPC_BLUEPRINT_PATH)
    old_objects = [require_asset(path) for path in OLD_MONTAGE_PATHS]
    new_objects = [require_asset(path) for path in NEW_MONTAGE_PATHS]

    before_compile = (
        unreal.AssetReferenceReplacementLibrary.replace_package_object_references(
            blueprint, old_objects, new_objects
        )
    )
    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    after_compile = (
        unreal.AssetReferenceReplacementLibrary.replace_package_object_references(
            blueprint, old_objects, new_objects
        )
    )
    unreal.EditorAssetLibrary.save_loaded_asset(blueprint, only_if_is_dirty=False)

    if unreal.EditorAssetLibrary.does_asset_exist(BACKUP_PATH):
        if not unreal.EditorAssetLibrary.delete_asset(BACKUP_PATH):
            raise RuntimeError(f"Could not remove invalid editor backup: {BACKUP_PATH}")
        unreal.log_warning(f"[OriginalMaidRepair] removed_invalid_backup={BACKUP_PATH}")

    unreal.log_warning(
        f"[OriginalMaidRepair] replacements_before_compile={before_compile} "
        f"replacements_after_compile={after_compile}"
    )
    unreal.log_warning("[OriginalMaidRepair] COMPLETE")


main()
