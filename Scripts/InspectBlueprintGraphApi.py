import unreal


blueprint = unreal.load_asset("/Game/Blueprints/BP_LLM_NPC_VRM")


def matching(value, terms):
    return ",".join(
        sorted(
            name
            for name in dir(value)
            if any(term in name.lower() for term in terms)
        )
    )


unreal.log_warning(
    "[BlueprintGraphApi] blueprint="
    + matching(blueprint, ["graph", "uber", "function", "node"])
)
unreal.log_warning(
    "[BlueprintGraphApi] library="
    + matching(unreal.BlueprintEditorLibrary, ["graph", "node", "reference", "replace"])
)
unreal.log_warning("[BlueprintGraphApi] COMPLETE")
