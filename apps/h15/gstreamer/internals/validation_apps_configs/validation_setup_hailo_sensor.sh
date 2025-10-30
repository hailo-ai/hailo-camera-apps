#!/bin/bash

# Default lens name
LENS_NAME="theia_sl410m"
MEDIALIB_CONFIG_BASE="/home/root/apps/internals/validation_apps_configs/resources/configs/stress_medialib_config.json"
PROJECT_NAME="hailo15h"
# Default resolution order (highest to lowest)
RESOLUTION_ORDER=("4k" "5mp" "4mp" "fhd")
RESOLUTION=""

# Function to display help message
show_help() {
    echo "Usage: setup_hailo_sensor.sh [OPTIONS]"
    echo
    echo "Description:"
    echo "  This script automatically detects the IMX sensor connected to the system"
    echo "  and configures the appropriate medialib settings by creating a symlink"
    echo "  to the sensor-specific configuration directory."
    echo
    echo "  The script will:"
    echo "  1. Detect the connected IMX sensor"
    echo "  2. Remove the existing medialib_configs symlink in /etc/imaging/cfg/"
    echo "  3. Create a new symlink pointing to the sensor-specific configuration"
    echo
    echo "Options:"
    echo "  --lens-name NAME    Specify the lens name to use (default: '$LENS_NAME')"
    echo "  --project NAME    Specify the project name to use (default: '$PROJECT_NAME')"
    echo "  --help              Display this help message and exit"
    echo
    exit 0
}


# Parse command line arguments
while [[ $# -gt 0 ]]; do
    key="$1"
    case $key in
        --lens-name)
            LENS_NAME="$2"
            shift 2
            ;;
        --project)
            PROJECT_NAME="$2"
            shift 2
            ;;
        --help)
            show_help
            ;;
        *)
            # Skip unknown options
            shift
            ;;
    esac
done

# Function to find sensor name by looking through video4linux subdevices
find_sensor_name() {
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

# Find the sensor name
SENSOR_NAME=$(find_sensor_name)
if [ -z "$SENSOR_NAME" ]; then
    echo "Error: No IMX sensor found"
    exit 1
fi
echo "Found sensor: $SENSOR_NAME"

# Check if /etc/imaging/cfg directory exists
if [ ! -d "/etc/imaging/cfg/" ]; then
    echo "Error: Directory /etc/imaging/cfg/ does not exist"
    exit 1
fi

echo "Searching for configuration directory for sensor: $SENSOR_NAME, lens: $LENS_NAME"

# Check if a directory for this sensor exists
if [ ! -d "/etc/imaging/cfg/$PROJECT_NAME/$SENSOR_NAME" ]; then
    echo "Error: No configuration directory found for sensor $SENSOR_NAME in /etc/imaging/cfg/$PROJECT_NAME/$SENSOR_NAME"
    exit 1
fi

# Check if the lens directory exists
if [ ! -d "/etc/imaging/cfg/$PROJECT_NAME/$SENSOR_NAME/$LENS_NAME" ]; then
    echo "Error: No configuration directory found for lens $LENS_NAME with sensor $SENSOR_NAME"
    exit 1
fi

# Find the resolution directory
RESOLUTION_DIR=""
if [ -n "$RESOLUTION" ]; then
    if [ -d "/etc/imaging/cfg/$PROJECT_NAME/$SENSOR_NAME/$LENS_NAME/$RESOLUTION" ]; then
        RESOLUTION_DIR="/etc/imaging/cfg/$PROJECT_NAME/$SENSOR_NAME/$LENS_NAME/$RESOLUTION"
    else
        echo "Error: Resolution directory $RESOLUTION not found under /etc/imaging/cfg/$PROJECT_NAME/$SENSOR_NAME/$LENS_NAME"
        exit 1
    fi
else
    # Pick the highest available resolution
    for res in "${RESOLUTION_ORDER[@]}"; do
        if [ -d "/etc/imaging/cfg/$PROJECT_NAME/$SENSOR_NAME/$LENS_NAME/$res" ]; then
            RESOLUTION_DIR="/etc/imaging/cfg/$PROJECT_NAME/$SENSOR_NAME/$LENS_NAME/$res"
            RESOLUTION="$res"
            break
        fi
    done
    if [ -z "$RESOLUTION_DIR" ]; then
        echo "Error: No supported resolution directory found under /etc/imaging/cfg/$PROJECT_NAME/$SENSOR_NAME/$LENS_NAME"
        exit 1
    fi
fi

CONFIG_DIR="$RESOLUTION_DIR"
echo "Found configuration directory: $CONFIG_DIR (resolution: $RESOLUTION)"

# Check if target medialib_configs directory exists
if [ ! -d "$CONFIG_DIR/medialib_configs" ]; then
    echo "Error: $CONFIG_DIR/medialib_configs directory does not exist"
    exit 1
fi

if ! cp "$CONFIG_DIR/medialib_configs/ai_example_medialib_config.json" "$MEDIALIB_CONFIG_BASE"; then
    echo "Error: Failed to copy from $CONFIG_DIR/medialib_configs/ai_example_medailib_config.json to $MEDIALIB_CONFIG_BASE"
    exit 1
fi

echo "Configuration setup complete"
