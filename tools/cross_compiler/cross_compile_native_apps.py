#!/usr/bin/env python
import argparse
import logging
import os
import sys
from pathlib import Path
import multiprocessing

from common import (FOLDER_NAME, Arch, MesonInstaller, Target,
                    install_compilers_apt_packages)

POSSIBLE_BUILD_TYPES = ['debug', 'release']
TAPPAS_WORKSPACE = FOLDER_NAME.parent.parent.resolve()


class H15NativeInstaller(MesonInstaller):
    INCLUDES = [
        '/usr/include/hailort',
        '/usr/include/gst-hailo/metadata',
    ]
    LIBARGS_TEMPLATE = "{}-std=c++17"

    def __init__(self, arch, target, build_type, toolchain_dir_path, build_lib='all',
                 install_to_rootfs=False, remote_machine_ip=None, clean_build_dir=False, platform='auto', install_profiles=False):
        super().__init__(arch=arch, build_type=build_type, src_build_dir=TAPPAS_WORKSPACE / "apps/h15/native",
                         toolchain_dir_path=toolchain_dir_path, remote_machine_ip=remote_machine_ip,
                         clean_build_dir=clean_build_dir, install_to_toolchain_rootfs=install_to_rootfs, platform=platform)
        self._target_platform = target
        self._build_lib = build_lib
        self._open_source_root = f'{TAPPAS_WORKSPACE}/core/open_source'
        self._platform = platform
        self._install_profiles = install_profiles
        # Now that we have the platform (either specified or auto-detected), set the output build directory
        self._hailort_cross_compiled_output_dir = FOLDER_NAME / f'{self._arch.value}-{self._platform}-media-library-build-{self._build_type}'

    def get_meson_build_folder(self):
        return 'native-apps'

    def get_libargs_line(self, rootfs_base_path):
        def get_includes(includes):
            include_string = ''
            for inc in includes:
                include_string += "-I{}{},".format(rootfs_base_path, inc)
            return include_string

        return self.LIBARGS_TEMPLATE.format(get_includes(self.INCLUDES))

    def get_meson_build_command(self):
        usr_path = '/usr' # This will be extended from toolchain rootfs base path

        rapidjson_root = f'{self._open_source_root}/rapidjson'

        required_sources = [rapidjson_root]

        if any(not Path(source).is_dir() for source in required_sources):
            raise FileNotFoundError(f"One or more of the external packages are missing. Please run {TAPPAS_WORKSPACE}/scripts/build_scripts/clone_external_packages.sh")

        build_cmd = ['meson', str(self._output_build_dir), '--buildtype', self._build_type,
                     '-Dlibargs={}'.format(self.get_libargs_line(self._toolchain_rootfs_base_path)),
                     '-Dprefix={}'.format(usr_path),
                     '-Dapps_install_dir=/home/root/apps',
                     '-Dplatform={}'.format(self._platform),
                     '-Dinstall_profiles={}'.format(self._install_profiles)]

        # Add the --reconfigure flag if build dir exists
        if self._output_build_dir.exists():
            build_cmd.append('--reconfigure')

        return build_cmd

    def detect_platform_from_toolchain(self):
        """Detect platform (15h or 15l) from hostname file in toolchain"""
        hostname_file = self._toolchain_rootfs_base_path / "etc" / "hostname"
        
        if not hostname_file.exists():
            raise FileNotFoundError(f"Hostname file {hostname_file} not found. Cannot detect platform.")
            
        try:
            with open(hostname_file, 'r') as f:
                hostname_content = f.read().strip()
                
            if "hailo15l" in hostname_content:
                self._logger.info("Detected platform: 15l")
                return "15l"
            elif "hailo15" in hostname_content:
                self._logger.info("Detected platform: 15h")
                return "15h"
            else:
                self._logger.warning(f"Unknown hostname content: {hostname_content}, defaulting to 15h")
                return "15h"
        except Exception as e:
            self._logger.warning(f"Error reading hostname file: {e}, defaulting to 15h")
            raise FileNotFoundError(f"Cannot determine platform from hostname file {hostname_file}.")


def parse_args():
    parser = argparse.ArgumentParser(description='Cross-compile TAPPAS')
    parser.add_argument('target', type=Target, choices=[Target.HAILO15], help='Target platform to compile to')
    parser.add_argument('build_type', choices=POSSIBLE_BUILD_TYPES, help='Build and compilation type')
    parser.add_argument('toolchain_dir_path', help='Toolchain directory path')
    parser.add_argument('--remote-machine-ip', help='remote machine ip')
    parser.add_argument('--clean-build-dir', action='store_true', help='Delete previous build cache (default false)', default=False)
    parser.add_argument('--install-to-rootfs', action='store_true', help='Install to rootfs (default false)', default=False)
    parser.add_argument('--check-req-packages', action='store_true', help='Install compiler packages (default false)', default=False)
    parser.add_argument('--limit-jobs', type=int, help='Limit the number of jobs for the build process', default=max(1, multiprocessing.cpu_count() - 2))
    parser.add_argument('--platform', help='Platform to compile for (15h or 15l)', default='15h')
    parser.add_argument('--install-profiles', action='store_true', help='Install all imaging profiles and config files, still requires --remote-machine-ip', default=False)

    return parser.parse_args()


if __name__ == '__main__':
    args = parse_args()
    logging.basicConfig(stream=sys.stdout, level=logging.INFO)

    if args.check_req_packages:
        install_compilers_apt_packages(Arch.ARMV8A)

    gst_installer = H15NativeInstaller(arch=Arch.ARMV8A, target=args.target, build_type=args.build_type,
                                        toolchain_dir_path=args.toolchain_dir_path,
                                        build_lib='all',
                                        remote_machine_ip=args.remote_machine_ip,
                                        clean_build_dir=args.clean_build_dir,
                                        install_to_rootfs=args.install_to_rootfs,
                                        platform=args.platform,
                                        install_profiles=args.install_profiles)
    gst_installer.build(args.limit_jobs)
