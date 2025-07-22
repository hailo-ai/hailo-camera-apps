#!/bin/bash
set -e

declare -A max_resolutions=(
    ["imx678"]="4k"
    ["imx675"]="5mp"
    ["imx715"]="4k"
    ["imx662"]="2k"
    ["imx664"]="4mp"
)

# Function to find sensor name by looking through video4linux subdevices
function find_sensor_name() {
    for entry in /sys/class/video4linux/*/name; do
        if [ -f "$entry" ]; then
            name=$(cat "$entry")
            if echo "$name" | grep -q "imx"; then
                # Extract the part before the first space
                echo "$name" | cut -d' ' -f1
                return 0
            fi
        fi
    done
    echo ""
    return 1
}

# Function to display usage information
function show_usage() {
    echo "Usage: $0 [OPTIONS]"
    echo
    echo "Options:"
    echo "  --mode, -m MODE             Set the mode (default: daylight)"
    echo "  --tuning, -t TUNING         Set the tuning extension (default: none)"
    echo "  --project, -p PROJECT       Set the project name (default: hailo15h)"
    echo "  --lens, -l LENS             Set the lens name (default: theia_sl410m)"
    echo "  --help, -h                  Show this help message"
    echo
    echo "Example:"
    echo "  $0 --mode daylight --tuning _r0225 --resolution 4k"
}

# Parse the command-line arguments
function parse_args() {
    # Default values
    MODE="daylight"
    TUNING_EXTENSION=""
    PROJECT="hailo15h"
    DEFAULT_SENSOR_NAME=$(find_sensor_name)
    if [ -z "$DEFAULT_SENSOR_NAME" ]; then
        echo "Warning: No IMX sensor found automatically"
        SENSOR=""
    else
        SENSOR="$DEFAULT_SENSOR_NAME"
    fi
    LENS_NAME="theia_sl410m"
    
    # Parse arguments
    while [[ $# -gt 0 ]]; do
        case $1 in
            --mode|-m)
                MODE="$2"
                shift 2
                ;;
            --tuning|-t)
                TUNING_EXTENSION="$2"
                shift 2
                ;;
            --project|-p)
                PROJECT="$2"
                shift 2
                ;;
            --lens|-l)
                LENS_NAME="$2"
                shift 2
                ;;
            --help|-h)
                show_usage
                exit 0
                ;;
            *)
                echo "Error: Unknown option: $1" >&2
                show_usage
                exit 1
                ;;
        esac
    done
    
    # Validate required arguments
    if [ -z "$SENSOR" ]; then
        echo "Error: Sensor name is required. Use --sensor option or ensure sensor can be auto-detected." >&2
        exit 1
    fi
    
    echo "Configuration:"
    echo "  Mode: $MODE"
    echo "  Tuning extension: ${TUNING_EXTENSION:-none}"
    echo "  Sensor: $SENSOR"
    echo "  Lens: $LENS_NAME"
}

function remove_symlink_and_copy_isp_configuration() {

    echo "Removing symlinks and copying ISP configuration files..."
    
    local MAX_RESOLUTION="${max_resolutions[$SENSOR]}"

    local SYMLINK_ISP_3A_CONFIG="/usr/bin/isp_3aconfig_0"
    local SYMLINK_ISP_SENSOR_ENTRY_0="/usr/bin/isp_sensor_entry_0"
    local SOURCE_3ACONFIG="/etc/imaging/cfg/$PROJECT/$SENSOR/$LENS_NAME/$MAX_RESOLUTION/profiles/$MODE/tuning$TUNING_EXTENSION/3aconfig.json"
    local SOURCE_SENSOR_ENTRY="/etc/imaging/cfg/$PROJECT/$SENSOR/$LENS_NAME/$MAX_RESOLUTION/profiles/$MODE/tuning$TUNING_EXTENSION/Sensor0_Entry.cfg"
    local DEST_3ACONFIG="/usr/bin/3aconfig.json"
    local DEST_SENSOR_ENTRY="/usr/bin/Sensor0_Entry.cfg"

    # Check if files exist
    if [[ ! -f "$SOURCE_3ACONFIG" ]]; then
        echo "Error: File not found: $SOURCE_3ACONFIG" >&2
        return 1
    fi

    if [[ ! -f "$SOURCE_SENSOR_ENTRY" ]]; then
        echo "Error: File not found: $SOURCE_SENSOR_ENTRY" >&2
        return 1
    fi

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

# Main execution
parse_args "$@"
remove_symlink_and_copy_isp_configuration
