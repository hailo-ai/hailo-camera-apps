################################################
# Profiles tree Installation
################################################
profiles_install_dir = '/etc/imaging/'
install_subdir('cfg', install_dir : profiles_install_dir)

# Install profiles
install_profiles = get_option('install_profiles')
if install_profiles
    fs = import('fs')
    config_files = run_command('find', '.', '-type', 'f', 
                            '!', '-name', 'meson.build',
                            '!', '-name', 'setup_hailo_sensor.sh').stdout().split()
    foreach f : config_files
        target_dir = fs.parent(f)
        install_data(f, install_dir: profiles_install_dir + target_dir)
    endforeach
endif

if platform == '15h'
    default_medialib_configs = 'hailo15h/imx678/theia_sl410m/4k/medialib_configs'
else
    default_medialib_configs = 'hailo15l/imx675/theia_sl410m/5mp/medialib_configs'
endif
install_symlink('medialib_configs', pointing_to: default_medialib_configs, install_dir: profiles_install_dir + 'cfg')

# Install setup_hailo_sensor.sh to /usr/bin
install_data('setup_hailo_sensor.sh', install_dir : '/usr/bin', install_mode: 'rwxr-xr-x')
