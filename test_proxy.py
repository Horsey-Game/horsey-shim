import ctypes, os
dll = ctypes.WinDLL(r"D:\dev\SHIM\build\version.dll")
dll.GetFileVersionInfoSizeW.argtypes = [ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_ulong)]
dll.GetFileVersionInfoSizeW.restype = ctypes.c_ulong
h = ctypes.c_ulong(0)
print("size:", dll.GetFileVersionInfoSizeW(r"C:\Windows\System32\notepad.exe", ctypes.byref(h)))
print(open(r"D:\dev\SHIM\build\SHIM.log").read())
