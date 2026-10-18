"""Find a test process's window without sending input to an existing game."""
def find_window(process_id, class_name):
    import ctypes
    from ctypes import wintypes
    user = ctypes.WinDLL('user32', use_last_error=True)
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user.EnumWindows.argtypes = [callback_type, wintypes.LPARAM]
    user.EnumWindows.restype = wintypes.BOOL
    user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user.GetClassNameA.argtypes = [wintypes.HWND, wintypes.LPSTR, ctypes.c_int]
    found = []

    @callback_type
    def visit(window, _):
        pid = wintypes.DWORD()
        user.GetWindowThreadProcessId(window, ctypes.byref(pid))
        if pid.value == process_id:
            name = ctypes.create_string_buffer(128)
            user.GetClassNameA(window, name, len(name))
            if name.value == class_name:
                found.append(window)
                return False
        return True

    user.EnumWindows(visit, 0)
    return found[0] if found else None
