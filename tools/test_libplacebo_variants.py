"""Load both packaged VP renderers in one process, in either order (Windows x64)."""
import argparse
import ctypes
import hashlib
import re
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("--build-root", type=Path, default=Path(__file__).resolve().parents[1] / "x64/Release")
parser.add_argument("--beta-first", action="store_true")
args = parser.parse_args()
root = args.build_root.resolve()
# Compare both binaries with the host's source ABI contract, not an old literal.
api_header = (Path(__file__).resolve().parents[1] /
              "src/VideoProcessor-Lib/vprenderer/LibplaceboRendererPluginApi.h")
api_versions = re.findall(
    r"^\s*static\s+constexpr\s+uint32_t\s+VP_LIBPLACEBO_PLUGIN_API_VERSION\s*=\s*(\d+)\s*;",
    api_header.read_text(encoding="utf-8"), re.MULTILINE)
assert len(api_versions) == 1, "Expected one renderer ABI version in the host header"
expected_api = int(api_versions[0])
kernel = ctypes.WinDLL("kernel32", use_last_error=True)
kernel.GetModuleHandleW.argtypes = [ctypes.c_wchar_p]
kernel.GetModuleHandleW.restype = ctypes.c_void_p
kernel.GetModuleFileNameW.argtypes = [ctypes.c_void_p, ctypes.c_wchar_p, ctypes.c_uint]
kernel.GetModuleFileNameW.restype = ctypes.c_uint
variants = [(False, "vprenderer", "VideoProcessorVPRenderer.dll", "libplacebo-360.dll"),
            (True, "vprenderer-beta", "VideoProcessorVPRendererBeta.dll", "libplacebo-377.dll")]
if args.beta_first:
    variants.reverse()
modules = []
for beta, directory, plugin_name, library_name in variants:
    folder = root / directory
    plugin = ctypes.CDLL(str(folder / plugin_name), winmode=0x100 | 0x800)
    modules.append(plugin)
    plugin.VideoProcessorLibplaceboIsBeta.restype = ctypes.c_bool
    plugin.VideoProcessorLibplaceboGetApiVersion.restype = ctypes.c_uint32
    assert plugin.VideoProcessorLibplaceboIsBeta() == beta, plugin_name
    assert plugin.VideoProcessorLibplaceboGetApiVersion() == expected_api, plugin_name
    handle = kernel.GetModuleHandleW(library_name)
    assert handle, library_name
    path = ctypes.create_unicode_buffer(32768)
    assert kernel.GetModuleFileNameW(handle, path, len(path))
    assert Path(path.value).resolve() == (folder / library_name).resolve(), path.value
    library = ctypes.CDLL(str(folder / library_name), winmode=0x100 | 0x800)
    library.pl_version.restype = ctypes.c_char_p
    print(f"PASS {plugin_name}: API {expected_api}; {library_name}; {library.pl_version().decode()}")
# Windows can reuse already loaded transitive DLLs by basename. Enforce identical
# shared runtimes instead of assuming directory separation isolates these DLLs.
for stable in (root / "vprenderer").glob("*.dll"):
    if stable.name.startswith(("libplacebo-", "VideoProcessor")):
        continue
    other = root / "vprenderer-beta" / stable.name
    if other.exists():
        digest = lambda p: hashlib.sha256(p.read_bytes()).digest()
        assert digest(stable) == digest(other), f"Shared runtime mismatch: {stable.name}"
print("PASS simultaneous loading and shared runtime identity")
