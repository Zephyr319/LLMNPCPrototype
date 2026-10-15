import unreal


SOURCE_MESH_PATH = "/Game/art/actions/Watering"
TARGET_MESH_PATH = "/Game/art/虚拟女仆/SK_（25_9_9更新）midars"
RETARGETER_PATH = "/Game/art/actions/RTG_Mixamo_To_midars"
DESTINATION_FOLDER = "/Game/art/actions"

SOURCE_ANIMATION_PATHS = [
    "/Game/art/actions/Nervously_Look_Around",
    "/Game/art/actions/Reacting",
    "/Game/art/actions/Surprised",
    "/Game/art/actions/Telling_A_Secret",
    "/Game/art/actions/Texting",
    "/Game/art/actions/Texting_And_Walking",
]


def require_asset(path):
    asset = unreal.load_asset(path)
    if not asset:
        raise RuntimeError(f"Missing asset: {path}")
    return asset


def main():
    source_mesh = require_asset(SOURCE_MESH_PATH)
    target_mesh = require_asset(TARGET_MESH_PATH)
    retargeter = require_asset(RETARGETER_PATH)

    expected_destination_paths = [
        f"{DESTINATION_FOLDER}/Anim_{path.rsplit('/', 1)[-1]}_Midars"
        for path in SOURCE_ANIMATION_PATHS
    ]
    existing_destination_paths = [
        path
        for path in expected_destination_paths
        if unreal.EditorAssetLibrary.does_asset_exist(path)
    ]
    if len(existing_destination_paths) == len(expected_destination_paths):
        for path in expected_destination_paths:
            animation = require_asset(path)
            skeleton = animation.get_editor_property("skeleton")
            unreal.log_warning(
                f"[EmbodiedRetarget] VALID {animation.get_path_name()} "
                f"skeleton={skeleton.get_name() if skeleton else 'none'} "
                f"length={animation.get_play_length():.3f}"
            )
        unreal.log_warning("[EmbodiedRetarget] ALL_OUTPUTS_ALREADY_EXIST")
        return
    if existing_destination_paths:
        raise RuntimeError(
            "Partial retarget output exists: " + ", ".join(existing_destination_paths)
        )

    source_assets = [require_asset(path) for path in SOURCE_ANIMATION_PATHS]
    source_asset_data = [
        unreal.EditorAssetLibrary.find_asset_data(path)
        for path in SOURCE_ANIMATION_PATHS
    ]
    if any(not asset_data.is_valid() for asset_data in source_asset_data):
        raise RuntimeError("Could not resolve one or more animation AssetData records")

    results = unreal.IKRetargetBatchOperation.duplicate_and_retarget(
        source_asset_data,
        source_mesh,
        target_mesh,
        retargeter,
        "",
        "",
        "Anim_",
        "_Midars",
        False,
        True,
    )

    if len(results) != len(source_assets):
        raise RuntimeError(
            f"Retarget count mismatch: expected={len(source_assets)} actual={len(results)}"
        )

    output_paths = []
    for result in results:
        asset = result.get_asset()
        if not asset:
            raise RuntimeError(f"Retarget returned an unloaded asset: {result}")
        source_path = asset.get_path_name().split(".", 1)[0]
        destination_path = f"{DESTINATION_FOLDER}/{asset.get_name()}"
        if source_path != destination_path:
            if unreal.EditorAssetLibrary.does_asset_exist(destination_path):
                raise RuntimeError(f"Destination asset already exists: {destination_path}")
            if not unreal.EditorAssetLibrary.rename_asset(
                source_path, destination_path
            ):
                raise RuntimeError(
                    f"Could not move retargeted asset: {source_path} -> {destination_path}"
                )
            asset = require_asset(destination_path)
        output_paths.append(asset.get_path_name())
        unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False)

    unreal.log_warning(
        "[EmbodiedRetarget] COMPLETE " + ", ".join(sorted(output_paths))
    )


main()
