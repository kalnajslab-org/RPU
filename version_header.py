"""
PlatformIO pre-build script that generates a C header containing the current
git version string as the RPU_VERSION preprocessor macro.

The output directory defaults to 'src' but can be overridden per-project via
the 'custom_version_header_dir' option in platformio.ini, e.g.:
    custom_version_header_dir = include
"""
import os
from SCons.Script import Import
from build_funcs import get_git_commit

Import("env")

version_file_name = "rpu_version.h"
header_define = "RPU_VERSION"

dest_dir = env.GetProjectOption("custom_version_header_dir", default="src")
header_file_path = os.path.join(env["PROJECT_DIR"], dest_dir, version_file_name)

version_str = get_git_commit()

os.makedirs(os.path.dirname(header_file_path), exist_ok=True)

with open(header_file_path, "w") as f:
    f.write(f'#ifndef {header_define}_H\n')
    f.write(f'#define {header_define}_H\n')
    f.write(f'#define {header_define} "{version_str}"\n')
    f.write(f'#endif // {header_define}_H\n')

print(f"Generated {header_file_path}: #define {header_define} \"{version_str}\"")
