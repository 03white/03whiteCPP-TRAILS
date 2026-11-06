"""通过 ctypes 调用 imgpipe 的 C API。

用法（在仓库根目录执行）：
    python src/20-sdk/20-01-picture/example/python_demo.py \
        build/msvc-debug/src/20-sdk/20-01-picture/bin/imgpipe.dll
"""

import ctypes
import sys
import threading


class Error(ctypes.Structure):
    _fields_ = [("code", ctypes.c_int), ("message", ctypes.c_char_p)]


STEP_FN = ctypes.CFUNCTYPE(None, ctypes.c_void_p)
LISTENER_FN = ctypes.CFUNCTYPE(
    None, ctypes.c_uint64, ctypes.c_int, Error, ctypes.c_void_p
)

STATES = {
    0: "Pending",
    1: "Running",
    2: "Succeeded",
    3: "Failed",
    4: "Skipped",
    5: "Cancelled",
}


def main(dll_path: str) -> int:
    lib = ctypes.CDLL(dll_path)

    lib.imgpipe_create.argtypes = [ctypes.c_uint32]
    lib.imgpipe_create.restype = ctypes.c_void_p
    lib.imgpipe_destroy.argtypes = [ctypes.c_void_p]
    lib.imgpipe_add_step.argtypes = [
        ctypes.c_void_p,
        ctypes.c_char_p,
        STEP_FN,
        ctypes.c_void_p,
    ]
    lib.imgpipe_add_step.restype = ctypes.c_uint64
    lib.imgpipe_add_dependency.argtypes = [
        ctypes.c_void_p,
        ctypes.c_uint64,
        ctypes.c_uint64,
    ]
    lib.imgpipe_add_dependency.restype = ctypes.c_int
    lib.imgpipe_set_listener.argtypes = [
        ctypes.c_void_p,
        LISTENER_FN,
        ctypes.c_void_p,
    ]
    lib.imgpipe_run.argtypes = [ctypes.c_void_p]
    lib.imgpipe_run.restype = ctypes.c_int
    lib.imgpipe_wait.argtypes = [ctypes.c_void_p]
    lib.imgpipe_cancel.argtypes = [ctypes.c_void_p]

    handle = lib.imgpipe_create(2)
    if not handle:
        print("imgpipe_create failed")
        return 1

    # 必须持有回调对象的引用，否则被 GC 后会崩溃
    keep_alive = []

    def make_step(name):
        @STEP_FN
        def step(_user_data):
            print(f"  [step] {name} on thread {threading.get_ident()}")

        keep_alive.append(step)
        return step

    @LISTENER_FN
    def listener(step_id, state, error, _user_data):
        print(
            f"  [event] step {step_id} -> {STATES.get(state, state)}"
            f" (thread {threading.get_ident()})"
        )

    keep_alive.append(listener)
    lib.imgpipe_set_listener(handle, listener, None)

    steps = {}
    for name in ("A", "B", "C", "D"):
        steps[name] = lib.imgpipe_add_step(
            handle, name.encode(), make_step(name), None
        )

    lib.imgpipe_add_dependency(handle, steps["B"], steps["A"])
    lib.imgpipe_add_dependency(handle, steps["C"], steps["A"])
    lib.imgpipe_add_dependency(handle, steps["D"], steps["B"])
    lib.imgpipe_add_dependency(handle, steps["D"], steps["C"])

    print(f"main thread = {threading.get_ident()}")
    print("run async...")
    if lib.imgpipe_run(handle) != 0:
        print("imgpipe_run failed")
        lib.imgpipe_destroy(handle)
        return 1

    lib.imgpipe_wait(handle)
    print("done")
    lib.imgpipe_destroy(handle)
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(sys.argv[1]))
