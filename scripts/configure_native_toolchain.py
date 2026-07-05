import os
import shutil
from glob import glob

from os.path import join

Import("env")

platform = env.PioPlatform()
toolchain_dir = platform.get_package_dir("toolchain-gccmingw32")

if not toolchain_dir:
    raise RuntimeError("toolchain-gccmingw32 package is not installed")

bin_dir = join(toolchain_dir, "bin")
env.PrependENVPath("PATH", bin_dir)

env.Replace(
    CC=join(bin_dir, "gcc"),
    CXX=join(bin_dir, "g++"),
    AR=join(bin_dir, "ar"),
    RANLIB=join(bin_dir, "ranlib"),
)

build_dir = env.subst("$BUILD_DIR")
os.makedirs(build_dir, exist_ok=True)
for dll_path in glob(join(bin_dir, "*.dll")):
    shutil.copy2(dll_path, join(build_dir, os.path.basename(dll_path)))
