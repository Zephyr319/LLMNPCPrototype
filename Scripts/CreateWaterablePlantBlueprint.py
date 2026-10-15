import unreal


ASSET_PATH = "/Game/Blueprints/Interaction/BP_WaterablePlant"


def main():
    if unreal.EditorAssetLibrary.does_asset_exist(ASSET_PATH):
        unreal.log(f"[WaterablePlant] Blueprint already exists: {ASSET_PATH}")
        return

    parent_class = unreal.WaterablePlantActor
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", parent_class)

    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    blueprint = asset_tools.create_asset(
        "BP_WaterablePlant",
        "/Game/Blueprints/Interaction",
        unreal.Blueprint,
        factory,
    )
    if not blueprint:
        raise RuntimeError("Could not create BP_WaterablePlant")

    if not unreal.EditorAssetLibrary.save_loaded_asset(blueprint, only_if_is_dirty=False):
        raise RuntimeError("Could not save BP_WaterablePlant")

    unreal.log(f"[WaterablePlant] created {ASSET_PATH}")


main()
