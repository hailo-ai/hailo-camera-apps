#!/bin/bash

# Constants for architecture detection
MACHINE_FILE_PATH="/sys/devices/soc0/machine"
HAILO_15_IDENTIFIER="Hailo-15"
HAILO_15L_IDENTIFIER="Hailo-15L"

# Function to detect Hailo architecture and set project name
get_hailo_architecture() {
    if [ ! -f "$MACHINE_FILE_PATH" ]; then
        echo "unknown"
        return
    fi
    
    local machine_info=$(cat "$MACHINE_FILE_PATH" 2>/dev/null)
    if [ -z "$machine_info" ]; then
        echo "unknown"
        return
    fi
    
    # Convert to lowercase for case-insensitive comparison
    local lower_machine_info=$(echo "$machine_info" | tr '[:upper:]' '[:lower:]')
    local lower_hailo_15l=$(echo "$HAILO_15L_IDENTIFIER" | tr '[:upper:]' '[:lower:]')
    local lower_hailo_15=$(echo "$HAILO_15_IDENTIFIER" | tr '[:upper:]' '[:lower:]')
    
    if echo "$lower_machine_info" | grep -q "$lower_hailo_15l"; then
        echo "hailo15l"
    elif echo "$lower_machine_info" | grep -q "$lower_hailo_15"; then
        echo "hailo15h"
    else
        echo "unknown"
    fi
}

# Detect architecture and set default project name
DETECTED_ARCH=$(get_hailo_architecture)
case "$DETECTED_ARCH" in
    "hailo15l")
        DEFAULT_PROJECT_NAME="hailo15l"
        ;;
    "hailo15h")
        DEFAULT_PROJECT_NAME="hailo15h"
        ;;
    *)
        DEFAULT_PROJECT_NAME="hailo15h"  # fallback to hailo15h
        echo "Warning: Could not detect Hailo architecture, defaulting to hailo15h"
        ;;
esac

# Default lens name
LENS_NAME="theia_sl410m"
PROJECT_NAME="$DEFAULT_PROJECT_NAME"
MEDIALIB_CONFIG_BASE="/etc/imaging/cfg/medialib_configs"

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
    echo "  1. Detect the Hailo architecture (Hailo-15 or Hailo-15L) to set default project"
    echo "  2. Detect the connected IMX sensor"
    echo "  3. Remove the existing medialib_configs symlink in /etc/imaging/cfg/"
    echo "  4. Create a new symlink pointing to the sensor-specific configuration"
    echo
    echo "Detected architecture: $DETECTED_ARCH"
    echo "Default project name: $DEFAULT_PROJECT_NAME"
    echo
    echo "Options:"
    echo "  --project NAME      Specify the project name to use (default: '$DEFAULT_PROJECT_NAME')"
    echo "  --lens-name NAME    Specify the lens name to use (default: '$LENS_NAME')"
    echo "  --resolution RES    Specify the resolution (default: highest available from 4k, 5mp, 4mp, fhd)"
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
if [ ! -d "/etc/imaging/cfg" ]; then
    echo "Error: Directory /etc/imaging/cfg does not exist"
    exit 1
fi

echo "Searching for configuration directory for sensor: $SENSOR_NAME, lens: $LENS_NAME"

# Check if a directory for this sensor exists
if [ ! -d "/etc/imaging/cfg/$PROJECT_NAME/$SENSOR_NAME" ]; then
    echo "Error: No configuration directory found for sensor $SENSOR_NAME in /etc/imaging/cfg/$PROJECT_NAME"
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

# Check if medialib_configs symlink exists and remove it
if [ -L "$MEDIALIB_CONFIG_BASE" ]; then
    echo "Removing existing medialib_configs symlink"
    rm -f "$MEDIALIB_CONFIG_BASE"
elif [ -e "$MEDIALIB_CONFIG_BASE" ]; then
    echo "Warning: $MEDIALIB_CONFIG_BASE exists but is not a symlink"
    rm -rf "$MEDIALIB_CONFIG_BASE"
fi

# Create new symlink pointing to sensor-specific medialib_configs
echo "Creating symlink from $MEDIALIB_CONFIG_BASE to $CONFIG_DIR/medialib_configs"
if ! ln -sf "$CONFIG_DIR/medialib_configs" "$MEDIALIB_CONFIG_BASE"; then
    echo "Error: Failed to create symlink"
    exit 1
fi

echo "Symlink created successfully"
echo "Configuration setup complete"
