#!/bin/bash
set -e

function init_variables() {
    print_help_if_needed $@
    script_dir=$(dirname $(realpath "$0"))
    source $script_dir/../../../../scripts/misc/checks_before_run.sh

    readonly POSTPROCESS_DIR="$TAPPAS_WORKSPACE/apps/gstreamer/x86/libs"
    readonly DEFAULT_POST_SO="$POSTPROCESS_DIR/libdepth_estimation.so"
    readonly DEFAULT_VIDEO_SOURCE="$TAPPAS_WORKSPACE/apps/gstreamer/x86/depth_estimation/resources/instance_segmentation.mp4"
    readonly DEFAULT_HEF_PATH="$TAPPAS_WORKSPACE/apps/gstreamer/x86/depth_estimation/resources/fast_depth.hef"


    input_source=$DEFAULT_VIDEO_SOURCE
    hef_path=$DEFAULT_HEF_PATH
    post_so=$DEFAULT_POST_SO
    print_gst_launch_only=false
    additonal_parameters=""
    
    hailo_bus_id=$(hailortcli scan | awk '{ print $NF }' | tail -n 1)
}

function print_usage() {
    echo "Depth estimation pipeline usage:"
    echo ""
    echo "Options:"
    echo "  --help                   Show this help"
    echo "  -i INPUT --input INPUT   Set the video source (default $input_source)"
    echo "  --show-fps               Print fps"
    echo "  --print-gst-launch       Print the ready gst-launch command without running it"
    exit 0
}

function print_help_if_needed() {
    while test $# -gt 0; do
        if [ "$1" = "--help" ] || [ "$1" == "-h" ]; then
            print_usage
        fi

        shift
    done
}

function parse_args() {
    while test $# -gt 0; do
        if [ "$1" = "--show-fps" ]; then
            echo "Printing fps"
            additonal_parameters="-v 2>&1 | grep hailo_display"
        elif [ "$1" = "--print-gst-launch" ]; then
            print_gst_launch_only=true
        elif [ "$1" = "--input" ] || [ "$1" == "-i" ]; then
            input_source="$2"
            shift
        else
            echo "Received invalid argument: $1. See expected arguments below:"
            print_usage
            exit 1
        fi

        shift
    done
}

init_variables $@
parse_args $@

# If the video provided is from a camera
if [[ $input_source =~ "/dev/video" ]]; then
    source_element="v4l2src device=$input_source name=src_0 ! videoflip video-direction=horiz"
else
    source_element="filesrc location=$input_source name=src_0 ! qtdemux ! h264parse ! avdec_h264 "
fi

PIPELINE="gst-launch-1.0 \
    $source_element ! queue ! videoconvert n-threads=2 ! queue ! \
    tee name=t ! queue leaky=no max-size-buffers=30 max-size-bytes=0 max-size-time=0 ! \
    aspectratiocrop aspect-ratio=1/1 ! queue ! videoscale ! queue ! \
    hailonet hef-path=$hef_path device-id=$hailo_bus_id debug=False is-active=true qos=false batch-size=1 ! \
    queue leaky=no max-size-buffers=30 max-size-bytes=0 max-size-time=0 ! \
    hailofilter2 so-path=$post_so qos=false ! videoconvert ! \
    queue leaky=no max-size-buffers=30 max-size-bytes=0 max-size-time=0 ! \
    hailooverlay qos=false ! \
    queue leaky=no max-size-buffers=30 max-size-bytes=0 max-size-time=0 ! \
    videoconvert ! fpsdisplaysink video-sink=ximagesink name=hailo_display sync=false text-overlay=false \
    t. ! queue leaky=no max-size-buffers=30 max-size-bytes=0 max-size-time=0 ! \
    videoscale ! video/x-raw, width=300, height=300 ! queue ! videoconvert ! \
    ximagesink sync=false ${additonal_parameters}"

echo "Running"
echo ${PIPELINE}

if [ "$print_gst_launch_only" = true ]; then
    exit 0
fi

eval ${PIPELINE}
