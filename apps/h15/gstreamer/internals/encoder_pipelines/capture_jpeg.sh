#!/bin/bash
set -e

CURRENT_DIR="$(dirname "$(realpath "${BASH_SOURCE[0]}")")"

# Cleanup function to stop config tuning when script exits
cleanup_on_exit() {
    # Prevent multiple calls to cleanup
    if [[ "${CLEANUP_DONE:-}" == "true" ]]; then
        return 0
    fi
    CLEANUP_DONE=true
    
    echo "Script terminating - stopping config tuning application..."
    /home/root/apps/manage_config_tuning.sh stop
}

# Set up exit trap
trap cleanup_on_exit EXIT SIGINT SIGTERM

function init_variables() {
    readonly RESOURCES_DIR="${CURRENT_DIR}/resources"
    readonly DEFAULT_FRONTEND_CONFIG_FILE_PATH="$RESOURCES_DIR/configs/frontend_config.json"
    readonly DEFAULT_ENCODER_CONFIG_PATH="$RESOURCES_DIR/configs/jpeg_encoder_config.json"

    encoder_config_path=$DEFAULT_ENCODER_CONFIG_PATH
    frontend_config_file_path=$DEFAULT_FRONTEND_CONFIG_FILE_PATH
    
    input_source="/dev/video0"
    max_buffers_size=5
    sync_pipeline=false
    print_gst_launch_only=false
    additional_parameters=""

    mode="daylight"
    tuning_extension=""
    project="hailo15h"
}

function print_usage() {
    echo "Hailo15 JPEG pipeline usage:"
    echo ""
    echo "Options:"
    echo "  --help                      Show this help"
    echo "  -i, --input INPUT           Set the camera source (default: $input_source)"
    echo "  --show-fps                  Print fps"
    echo "  --print-gst-launch          Print the ready gst-launch command without running it"
    echo "  --mode                      mode (e.g., daylight)"
    echo "  --platform                  Set the platform (default 15h, options: 15h, 15l)"
    echo "  --tuning                    tuning extension - relevant only for denoise (e.g., _r0225)"
    echo "  --project                   project name (e.g., hailo15h)"
    exit 0
}

function parse_args() {
    while test $# -gt 0; do
        case "$1" in
            --help|-h)
                print_usage
                ;;
            --print-gst-launch)
                print_gst_launch_only=true
                ;;
            --show-fps)
                additional_parameters="-v | grep hailo_display"
                ;;
            -i|--input)
                input_source="$2"
                shift
                ;;
            --mode)
                mode="$2"
                shift
                ;;
            --platform)
                platform="$2"
                shift
                ;;
            --tuning)
                tuning_extension="$2"
                shift
                ;;
            --project)
                project="$2"
                shift
                ;;
            *)
                echo "Received invalid argument: $1"
                print_usage
                ;;
        esac
        shift
    done
}

init_variables $@
parse_args $@

PIPELINE="gst-launch-1.0 \
        v4l2src device=$input_source io-mode=dmabuf num-buffers=10 ! video/x-raw,format=NV12,width=3840,height=2160, framerate=30/1 ! \
        queue leaky=downstream max-size-buffers=$max_buffers_size max-size-bytes=0 max-size-time=0 ! \
        hailofrontend config-file-path=$frontend_config_file_path name=frontend \
        frontend. ! \
        queue leaky=no max-size-buffers=$max_buffers_size max-size-bytes=0 max-size-time=0 ! \
        hailoencodebin config-file-path=$encoder_config_path ! \
        tee name=jpeg_tee \
        jpeg_tee. ! \
            queue leaky=no max-size-buffers=$max_buffers_size max-size-bytes=0 max-size-time=0 ! \
            multifilesink location=frame%d.jpg \
        jpeg_tee. ! \
            queue leaky=no max-size-buffers=$max_buffers_size max-size-bytes=0 max-size-time=0 ! \
            fpsdisplaysink fps-update-interval=2000 video-sink=fakesink name=hailo_display sync=$sync_pipeline text-overlay=false \
        ${additional_parameters}"

/home/root/apps/manage_config_tuning.sh start "$mode"
if [ $? -ne 0 ]; then
    echo "Failed to start config tuning application."
    exit 1
fi

echo "Running pipeline with MODE=${mode:-default}"
echo ${PIPELINE}

if [ "$print_gst_launch_only" = true ]; then
    exit 0
fi

eval ${PIPELINE}
