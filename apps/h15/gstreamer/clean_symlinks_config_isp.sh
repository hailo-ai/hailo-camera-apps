
#!/bin/bash
set -e

function remove_symlink_and_copy_isp_configuration() {
    echo "Removing symlinks and copying ISP configuration files..."
    # Change here to configure the mode
    # Supported modes: daylight, HDR, lowlight
    local MODE="daylight"
    local SYMLINK_ISP_3A_CONFIG="/usr/bin/isp_3aconfig_0"
    local SYMLINK_ISP_SENSOR_ENTRY_0="/usr/bin/isp_sensor_entry_0"
    local SOURCE_3ACONFIG="/etc/imaging/cfg/imx678/theia_sl410m/4k/profiles/$MODE/tuning/3aconfig.json"
    local SOURCE_SENSOR_ENTRY="/etc/imaging/cfg/imx678/theia_sl410m/4k/profiles/$MODE/tuning/Sensor0_Entry.cfg"
    local DEST_3ACONFIG="/usr/bin/3aconfig.json"
    local DEST_SENSOR_ENTRY="/usr/bin/Sensor0_Entry.cfg"

    # Remove symlinks
    if [ -L "$SYMLINK_ISP_3A_CONFIG" ]; then
        echo "Removing symlink: $SYMLINK_ISP_3A_CONFIG"
        rm "$SYMLINK_ISP_3A_CONFIG"
    fi

    if [ -L "$SYMLINK_ISP_SENSOR_ENTRY_0" ]; then
        echo "Removing symlink: $SYMLINK_ISP_SENSOR_ENTRY_0"
        rm "$SYMLINK_ISP_SENSOR_ENTRY_0"
    fi

    # Copy the files
    echo "Copying $SOURCE_3ACONFIG to $DEST_3ACONFIG"
    cp "$SOURCE_3ACONFIG" "$DEST_3ACONFIG"

    echo "Copying $SOURCE_SENSOR_ENTRY to $DEST_SENSOR_ENTRY"
    cp "$SOURCE_SENSOR_ENTRY" "$DEST_SENSOR_ENTRY"

    echo "Configuration completed."
}

remove_symlink_and_copy_isp_configuration