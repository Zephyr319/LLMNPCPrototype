import unreal


FOLDERS = [
    "/Game/art/actions",
    "/Game/KawaiiAnimations/Animations",
]


for folder in FOLDERS:
    for object_path in unreal.EditorAssetLibrary.list_assets(
        folder, recursive=True, include_folder=False
    ):
        asset = unreal.load_asset(object_path)
        if not asset:
            continue
        if not isinstance(asset, (unreal.AnimationAsset, unreal.AnimBlueprint)):
            continue

        skeleton = None
        try:
            skeleton = asset.get_editor_property("skeleton")
        except Exception:
            try:
                skeleton = asset.get_editor_property("target_skeleton")
            except Exception:
                pass

        unreal.log_warning(
            f"[AnimationSourceAudit] asset={asset.get_path_name()} "
            f"class={asset.get_class().get_name()} "
            f"skeleton={skeleton.get_path_name() if skeleton else 'None'}"
        )

unreal.log_warning("[AnimationSourceAudit] COMPLETE")
