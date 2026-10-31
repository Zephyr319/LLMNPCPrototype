import unreal


ACTION_SET_PATH = "/Game/Blueprints/Cafe/DA_NPCAutonomyActionSet"
ANIMATION_ROOT = "/Game/Characters/OriginalCafeMaid/Animations"
ACTION_ANIMATION_PATHS = {
    "phone_at_counter": f"{ANIMATION_ROOT}/Anim_Texting_While_Standing_OCM",
    "daydream_at_counter": f"{ANIMATION_ROOT}/Anim_KA_Idle04_LookAtFeet_OCM",
    "look_around": f"{ANIMATION_ROOT}/Anim_KA_Idle02_LookLeftAndRight_OCM",
    "doze_at_counter": f"{ANIMATION_ROOT}/Anim_Yawn_OCM",
    "water_flower": f"{ANIMATION_ROOT}/Anim_WaterPlant_OCM",
    "check_supplies": f"{ANIMATION_ROOT}/Anim_Searching_Files_High_OCM",
    "review_shift_notes": (
        f"{ANIMATION_ROOT}/Anim_Standing_Using_Touchscreen_Tablet_OCM"
    ),
    "angry_at_counter": f"{ANIMATION_ROOT}/Anim_Angry_OCM",
    "hum_at_counter_placeholder": (
        f"{ANIMATION_ROOT}/Anim_KA_Idle08_ComeUpWithAnIdea_OCM"
    ),
    "straighten_uniform_placeholder": (
        f"{ANIMATION_ROOT}/Anim_KA_Idle03_LookAtHands_OCM"
    ),
    "inspect_cafe_placeholder": (
        f"{ANIMATION_ROOT}/Anim_KA_Idle02_LookLeftAndRight_OCM"
    ),
    "organize_counter_placeholder": f"{ANIMATION_ROOT}/Anim_CleanCounter_OCM",
    "inspect_table_area": (
        f"{ANIMATION_ROOT}/Anim_KA_Idle02_LookLeftAndRight_OCM"
    ),
    "tidy_table_area": f"{ANIMATION_ROOT}/Anim_WipeDiningTable_OCM",
    "collect_tableware": f"{ANIMATION_ROOT}/Anim_PickUpDishes_OCM",
}


def require_asset(path):
    asset = unreal.load_asset(path)
    if not asset:
        raise RuntimeError(f"Missing required asset: {path}")
    return asset


action_set = require_asset(ACTION_SET_PATH)
activity_ids = list(ACTION_ANIMATION_PATHS.keys())
animations = [
    require_asset(ACTION_ANIMATION_PATHS[activity_id])
    for activity_id in activity_ids
]
updated = unreal.AssetReferenceReplacementLibrary.set_autonomy_action_animations(
    action_set, activity_ids, animations
)
if updated != len(activity_ids):
    raise RuntimeError(
        f"Expected {len(activity_ids)} action updates, received {updated}"
    )
unreal.EditorAssetLibrary.save_loaded_asset(action_set, only_if_is_dirty=False)
unreal.log_warning(f"[OriginalMaidActionRepair] updated={updated}")
unreal.log_warning("[OriginalMaidActionRepair] COMPLETE")
