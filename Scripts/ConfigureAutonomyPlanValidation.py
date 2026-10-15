import unreal


ACTION_SET_PATH = "/Game/Blueprints/Cafe/DA_NPCAutonomyActionSet"
WORLD_STATE_TAG = "dining_area_needs_tidy"
TASK_GROUP = "dining_area_reset"
VALIDATION_ACTION_IDS = {
    "inspect_table_area",
    "tidy_table_area",
    "record_tidy_completion",
    "collect_tableware",
}


def make_task_step(
    activity_id,
    display_name,
    description,
    order,
    target,
    animation_path,
    fallback_seconds,
    energy_delta,
    clears_world_state=False,
):
    activity = unreal.NPCAutonomyActivityDefinition()
    activity.set_editor_property("activity_id", activity_id)
    activity.set_editor_property("display_name", unreal.Text(display_name))
    activity.set_editor_property("llm_description", description)
    activity.set_editor_property("category", unreal.NPCAutonomyActivityCategory.TASK)
    activity.set_editor_property("task_group", TASK_GROUP)
    activity.set_editor_property("task_order", order)
    activity.set_editor_property("required_task_step", True)
    activity.set_editor_property(
        "execution_mode", unreal.NPCAutonomyExecutionMode.BUILT_IN_SEQUENCE
    )
    activity.set_editor_property("target_scene_tag", target)
    activity.set_editor_property("animation", unreal.load_asset(animation_path))
    activity.set_editor_property("animation_slot_name", "DefaultSlot")
    activity.set_editor_property("animation_play_rate", 1.0)
    activity.set_editor_property("animation_fallback_seconds", fallback_seconds)
    activity.set_editor_property("cooldown_seconds", 5.0)
    activity.set_editor_property("stimulation_gain", 4.0)
    activity.set_editor_property("duty_relief", 0.0 if not clears_world_state else 50.0)
    activity.set_editor_property("energy_delta", energy_delta)
    activity.set_editor_property("minimum_energy", 10.0)
    activity.set_editor_property("required_world_state_tag", WORLD_STATE_TAG)
    activity.set_editor_property(
        "world_state_tag_to_clear_on_success",
        WORLD_STATE_TAG if clears_world_state else "None",
    )
    activity.set_editor_property(
        "return_to_counter_after_completion", activity_id == "collect_tableware"
    )
    activity.set_editor_property("enabled", True)
    return activity


def main():
    action_set = unreal.load_asset(ACTION_SET_PATH)
    if not action_set:
        raise RuntimeError(f"Could not load {ACTION_SET_PATH}")

    activities = [
        activity
        for activity in action_set.get_editor_property("activities")
        if activity.get_editor_property("activity_id") not in VALIDATION_ACTION_IDS
    ]

    activities.extend(
        [
            make_task_step(
                "inspect_table_area",
                "Inspect Table Area",
                "Required phase 1 of dining_area_reset: go to Table_01 and inspect the table area before tidying it.",
                1,
                "Table_01",
                "/Game/art/虚拟女仆/Anim_KA_Idle02_LookLeftAndRight",
                6.0,
                -1.0,
            ),
            make_task_step(
                "tidy_table_area",
                "Tidy Table Area",
                "Required phase 2 of dining_area_reset: wipe the inspected Table_01 area before collecting its used tableware.",
                2,
                "Table_01",
                "/Game/art/actions/Anim_WipeDiningTable_Midars",
                11.4,
                -3.0,
            ),
            make_task_step(
                "collect_tableware",
                "Collect Used Tableware",
                "Required final phase 3 of dining_area_reset: collect the used bowl from Table_01, carry it back, and place it on the counter.",
                3,
                "Table_01",
                "/Game/art/actions/Anim_PickUpDishes_Midars",
                6.1,
                -2.0,
                clears_world_state=True,
            ),
        ]
    )

    action_set.set_editor_property("activities", activities)
    if not unreal.EditorAssetLibrary.save_loaded_asset(action_set, only_if_is_dirty=False):
        raise RuntimeError(f"Could not save {ACTION_SET_PATH}")

    unreal.log(
        f"[AutonomyPlanTest] configured {len(activities)} activities in {ACTION_SET_PATH}"
    )


main()
