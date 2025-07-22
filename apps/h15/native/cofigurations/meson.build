################################################
# Profiles tree Installation
################################################
profiles_install_dir = '/etc/imaging/'
install_subdir('cfg', install_dir : profiles_install_dir)

install_symlink('medialib_configs', pointing_to: 'hailo15h/imx678/theia_sl410m/medialib_configs', install_dir: profiles_install_dir + 'cfg')

# Install setup_hailo_sensor.sh to /usr/bin
install_data('setup_hailo_sensor.sh', install_dir : '/usr/bin', install_mode: 'rwxr-xr-x')
