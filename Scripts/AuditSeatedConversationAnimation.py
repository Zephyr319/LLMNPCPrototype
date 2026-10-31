import unreal


ANIMATION_PATH = (
    "/Game/Characters/OriginalCafeMaid/Animations/Anim_Female_Sit_Talk_OCM"
)
EXPECTED_SKELETON_PATH = (
    "/Game/Characters/OriginalCafeMaid/SKEL_OriginalCafeMaid_v1_0"
)


animation = unreal.load_asset(ANIMATION_PATH)
if not animation:
    raise RuntimeError(f"Missing seated conversation animation: {ANIMATION_PATH}")

skeleton = animation.get_editor_property("skeleton")
if not skeleton:
    raise RuntimeError("Seated conversation animation has no skeleton")

skeleton_path = skeleton.get_path_name().split(".", 1)[0]
if skeleton_path != EXPECTED_SKELETON_PATH:
    raise RuntimeError(
        f"Unexpected seated animation skeleton: {skeleton_path}"
    )

unreal.log_warning(
    f"[SeatedConversationAudit] PASS animation={animation.get_path_name()} "
    f"skeleton={skeleton.get_path_name()} length={animation.get_play_length():.3f}"
)
