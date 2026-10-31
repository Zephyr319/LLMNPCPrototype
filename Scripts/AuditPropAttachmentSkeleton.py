import unreal


MESH_PATH = "/Game/Characters/OriginalCafeMaid/SK_OriginalCafeMaid_v1_0"
SKELETON_PATH = "/Game/Characters/OriginalCafeMaid/SKEL_OriginalCafeMaid_v1_0"


mesh = unreal.load_asset(MESH_PATH)
skeleton = unreal.load_asset(SKELETON_PATH)
if not mesh or not skeleton:
    raise RuntimeError("Original cafe maid mesh or skeleton is missing")

reference_pose = skeleton.get_reference_pose()
bone_names = [str(name) for name in reference_pose.get_bone_names()]
hand_candidates = [
    name
    for name in bone_names
    if any(token in name.lower() for token in ("hand", "wrist", "palm"))
]

socket_names = []
try:
    sockets = skeleton.get_editor_property("sockets") or []
    socket_names = [str(socket.get_editor_property("socket_name")) for socket in sockets]
except Exception as exc:
    unreal.log_warning(f"[PropSocketAudit] skeleton socket inspection unavailable={exc}")

mesh_socket_names = []
try:
    mesh_sockets = mesh.get_editor_property("mesh_only_socket_list") or []
    mesh_socket_names = [
        str(socket.get_editor_property("socket_name")) for socket in mesh_sockets
    ]
except Exception as exc:
    unreal.log_warning(f"[PropSocketAudit] mesh socket inspection unavailable={exc}")

unreal.log_warning(f"[PropSocketAudit] mesh={mesh.get_path_name()}")
unreal.log_warning(f"[PropSocketAudit] skeleton={skeleton.get_path_name()}")
unreal.log_warning(f"[PropSocketAudit] bone_count={len(bone_names)}")
unreal.log_warning(f"[PropSocketAudit] hand_bones={','.join(hand_candidates)}")
unreal.log_warning(f"[PropSocketAudit] skeleton_sockets={','.join(socket_names)}")
unreal.log_warning(f"[PropSocketAudit] mesh_sockets={','.join(mesh_socket_names)}")
unreal.log_warning("[PropSocketAudit] COMPLETE")
