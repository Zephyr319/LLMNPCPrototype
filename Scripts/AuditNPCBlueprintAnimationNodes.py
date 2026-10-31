import unreal


BLUEPRINT_PATH = "/Game/Blueprints/BP_LLM_NPC_VRM"
blueprint = unreal.load_asset(BLUEPRINT_PATH)
if not blueprint:
    raise RuntimeError(f"Missing blueprint: {BLUEPRINT_PATH}")


graphs = []
for property_name in ("ubergraph_pages", "function_graphs", "macro_graphs"):
    try:
        graphs.extend(list(blueprint.get_editor_property(property_name)))
    except Exception as exc:
        unreal.log_warning(
            f"[NPCAnimationNodeAudit] graph_property={property_name} unavailable={exc}"
        )

event_graph = unreal.BlueprintEditorLibrary.find_event_graph(blueprint)
if event_graph and event_graph not in graphs:
    graphs.append(event_graph)

unreal.log_warning(
    f"[NPCAnimationNodeAudit] blueprint={BLUEPRINT_PATH} graph_count={len(graphs)}"
)

for graph in graphs:
    try:
        nodes = list(graph.get_editor_property("nodes"))
    except Exception as exc:
        unreal.log_warning(
            f"[NPCAnimationNodeAudit] graph={graph.get_name()} nodes_unavailable={exc}"
        )
        continue

    for node in nodes:
        try:
            pins = list(node.get_editor_property("pins"))
        except Exception:
            continue
        for pin in pins:
            try:
                default_object = pin.get_editor_property("default_object")
            except Exception:
                continue
            if not isinstance(default_object, unreal.AnimationAsset):
                continue
            unreal.log_warning(
                f"[NPCAnimationNodeAudit] graph={graph.get_name()} "
                f"node={node.get_name()} class={node.get_class().get_name()} "
                f"pin={pin.get_editor_property('pin_name')} "
                f"asset={default_object.get_path_name()}"
            )

unreal.log_warning("[NPCAnimationNodeAudit] COMPLETE")
