import unreal


ACTION_SET_PATH = "/Game/Blueprints/Cafe/DA_NPCAutonomyActionSet"


def require_asset(path):
    asset = unreal.load_asset(path)
    if not asset:
        raise RuntimeError(f"Missing asset: {path}")
    return asset


def set_animation(activity, path, fallback_seconds):
    activity.set_editor_property("animation", require_asset(path))
    activity.set_editor_property("animation_fallback_seconds", fallback_seconds)


def configure_common(
    activity,
    activity_id,
    display_name,
    description,
    category,
    target,
    animation_path,
    fallback_seconds,
    cooldown_seconds,
    stimulation_gain,
    energy_delta,
    minimum_energy,
):
    activity.set_editor_property("activity_id", activity_id)
    activity.set_editor_property("display_name", unreal.Text(display_name))
    activity.set_editor_property("llm_description", description)
    activity.set_editor_property("category", category)
    activity.set_editor_property(
        "execution_mode", unreal.NPCAutonomyExecutionMode.BUILT_IN_SEQUENCE
    )
    activity.set_editor_property("target_scene_tag", target)
    set_animation(activity, animation_path, fallback_seconds)
    activity.set_editor_property("animation_slot_name", "DefaultSlot")
    activity.set_editor_property("animation_play_rate", 1.0)
    activity.set_editor_property("cooldown_seconds", cooldown_seconds)
    activity.set_editor_property("stimulation_gain", stimulation_gain)
    activity.set_editor_property("duty_relief", 0.0)
    activity.set_editor_property("energy_delta", energy_delta)
    activity.set_editor_property("minimum_energy", minimum_energy)
    activity.set_editor_property("return_to_counter_after_completion", False)
    activity.set_editor_property("enabled", True)


def main():
    action_set = require_asset(ACTION_SET_PATH)
    activities = list(action_set.get_editor_property("activities"))
    by_id = {
        activity.get_editor_property("activity_id").casefold(): activity
        for activity in activities
    }

    phone = by_id["phone_at_counter"]
    phone.set_editor_property("display_name", unreal.Text("Text on Phone at Counter"))
    phone.set_editor_property(
        "llm_description",
        "Use her phone briefly at the counter when boredom is high and enough energy remains.",
    )
    phone.set_editor_property(
        "ambient_drive", unreal.NPCAutonomyAmbientDrive.BOREDOM_RELIEF
    )
    phone.set_editor_property("ambient_base_weight", 9.0)
    set_animation(
        phone, "/Game/art/actions/Anim_Texting_While_Standing_Midars", 22.0
    )
    phone.set_editor_property("cooldown_seconds", 70.0)
    phone.set_editor_property("stimulation_gain", 12.0)
    phone.set_editor_property("energy_delta", -3.0)
    phone.set_editor_property("minimum_energy", 20.0)

    daydream = by_id["daydream_at_counter"]
    daydream.set_editor_property("ambient_drive", unreal.NPCAutonomyAmbientDrive.NEUTRAL)
    daydream.set_editor_property("ambient_base_weight", 5.0)
    daydream.set_editor_property("cooldown_seconds", 50.0)
    daydream.set_editor_property("stimulation_gain", 3.0)
    daydream.set_editor_property("energy_delta", 2.0)

    look_around = by_id["look_around"]
    look_around.set_editor_property(
        "ambient_drive", unreal.NPCAutonomyAmbientDrive.CURIOSITY
    )
    look_around.set_editor_property("ambient_base_weight", 6.0)
    look_around.set_editor_property("cooldown_seconds", 45.0)
    look_around.set_editor_property("stimulation_gain", 5.0)
    look_around.set_editor_property("energy_delta", -1.0)

    doze = by_id["doze_at_counter"]
    doze.set_editor_property("display_name", unreal.Text("Yawn at Counter"))
    doze.set_editor_property(
        "llm_description",
        "Pause and yawn at the counter when energy is low; this is a fatigue response, not boredom relief.",
    )
    doze.set_editor_property(
        "ambient_drive", unreal.NPCAutonomyAmbientDrive.ENERGY_RECOVERY
    )
    doze.set_editor_property("ambient_base_weight", 5.0)
    set_animation(doze, "/Game/art/actions/Anim_Yawn_Midars", 8.5)
    doze.set_editor_property("cooldown_seconds", 90.0)
    doze.set_editor_property("stimulation_gain", 1.0)
    doze.set_editor_property("energy_delta", 5.0)

    water = by_id["water_flower"]
    water.set_editor_property("display_name", unreal.Text("Water Flowers"))
    water.set_editor_property(
        "llm_description",
        "Go to a plant that needs water, water it, and clear its local task state.",
    )
    set_animation(water, "/Game/art/actions/Anim_WaterPlant_Midars", 5.7)

    inspect_table = by_id["inspect_table_area"]
    set_animation(
        inspect_table,
        "/Game/art/虚拟女仆/Anim_KA_Idle02_LookLeftAndRight",
        6.0,
    )

    tidy_table = by_id["tidy_table_area"]
    tidy_table.set_editor_property("display_name", unreal.Text("Wipe Dining Table"))
    tidy_table.set_editor_property(
        "llm_description",
        "Wipe and clear the dining table after inspecting the used table area.",
    )
    set_animation(tidy_table, "/Game/art/actions/Anim_WipeDiningTable_Midars", 11.4)

    collect_tableware = by_id.get("collect_tableware")
    if collect_tableware:
        collect_tableware.set_editor_property(
            "display_name", unreal.Text("Collect Used Tableware")
        )
        collect_tableware.set_editor_property(
            "llm_description",
            "Collect the used bowl from the dining table, carry it back, and place it on the counter.",
        )
        set_animation(
            collect_tableware,
            "/Game/art/actions/Anim_PickUpDishes_Midars",
            6.1,
        )

    def upsert(activity_id):
        existing = by_id.get(activity_id.casefold())
        if existing:
            return existing
        created = unreal.NPCAutonomyActivityDefinition()
        activities.append(created)
        by_id[activity_id.casefold()] = created
        return created

    hum = upsert("hum_at_counter_placeholder")
    configure_common(
        hum,
        "hum_at_counter_placeholder",
        "Hum at Counter",
        "Hum softly at the counter while remaining available for service.",
        unreal.NPCAutonomyActivityCategory.LEISURE,
        "counter",
        "/Game/art/虚拟女仆/Anim_KA_Idle08_ComeUpWithAnIdea",
        5.0,
        50.0,
        3.0,
        -1.0,
        0.0,
    )
    hum.set_editor_property(
        "ambient_drive", unreal.NPCAutonomyAmbientDrive.POSITIVE_EMOTION
    )
    hum.set_editor_property("ambient_base_weight", 5.0)

    straighten_uniform = upsert("straighten_uniform_placeholder")
    configure_common(
        straighten_uniform,
        "straighten_uniform_placeholder",
        "Straighten Uniform",
        "Briefly straighten her uniform during a quiet moment.",
        unreal.NPCAutonomyActivityCategory.LEISURE,
        "counter",
        "/Game/art/虚拟女仆/Anim_KA_Idle03_LookAtHands",
        9.4,
        45.0,
        2.0,
        -0.5,
        0.0,
    )
    straighten_uniform.set_editor_property(
        "ambient_drive", unreal.NPCAutonomyAmbientDrive.NEUTRAL
    )
    straighten_uniform.set_editor_property("ambient_base_weight", 4.0)

    inspect_cafe = upsert("inspect_cafe_placeholder")
    configure_common(
        inspect_cafe,
        "inspect_cafe_placeholder",
        "Inspect Cafe",
        "Deliberately inspect the cafe and check whether the room looks orderly.",
        unreal.NPCAutonomyActivityCategory.PREFERENCE,
        "counter",
        "/Game/art/虚拟女仆/Anim_KA_Idle02_LookLeftAndRight",
        6.0,
        60.0,
        2.0,
        -1.0,
        0.0,
    )

    wipe_counter = upsert("organize_counter_placeholder")
    configure_common(
        wipe_counter,
        "organize_counter_placeholder",
        "Wipe Counter",
        "Wipe and organize the counter as a self-initiated housekeeping preference.",
        unreal.NPCAutonomyActivityCategory.PREFERENCE,
        "counter",
        "/Game/art/actions/Anim_CleanTable_Midars",
        11.3,
        90.0,
        5.0,
        -2.0,
        10.0,
    )

    check_supplies = upsert("check_supplies")
    configure_common(
        check_supplies,
        "check_supplies",
        "Check Counter Supplies",
        "Inspect and sort the files or supplies at the counter as a self-initiated work preference.",
        unreal.NPCAutonomyActivityCategory.PREFERENCE,
        "counter",
        "/Game/art/actions/Anim_Searching_Files_High_Midars",
        9.8,
        120.0,
        5.0,
        -3.0,
        20.0,
    )

    review_notes = upsert("review_shift_notes")
    configure_common(
        review_notes,
        "review_shift_notes",
        "Review Shift Notes",
        "Review work notes on a tablet at the counter when she chooses to organize upcoming duties.",
        unreal.NPCAutonomyActivityCategory.PREFERENCE,
        "counter",
        "/Game/art/actions/Anim_Standing_Using_Touchscreen_Tablet_Midars",
        33.2,
        180.0,
        6.0,
        -4.0,
        25.0,
    )

    angry_expression = upsert("angry_at_counter")
    configure_common(
        angry_expression,
        "angry_at_counter",
        "Show Brief Frustration",
        "Briefly show frustration only while the current emotional state is negative.",
        unreal.NPCAutonomyActivityCategory.EXPRESSION,
        "counter",
        "/Game/art/actions/Anim_Angry_Midars",
        19.2,
        180.0,
        1.0,
        -4.0,
        10.0,
    )
    angry_expression.set_editor_property(
        "ambient_drive", unreal.NPCAutonomyAmbientDrive.NEGATIVE_EMOTION
    )
    angry_expression.set_editor_property("ambient_base_weight", 4.0)

    action_set.set_editor_property("activities", activities)
    if not unreal.EditorAssetLibrary.save_loaded_asset(
        action_set, only_if_is_dirty=False
    ):
        raise RuntimeError(f"Could not save {ACTION_SET_PATH}")

    unreal.log_warning(
        f"[ImportedActionConfig] COMPLETE activities={len(activities)}"
    )


main()
