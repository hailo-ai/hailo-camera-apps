#!/bin/bash
set -e

CURRENT_DIR="$(dirname "$(realpath "${BASH_SOURCE[0]}")")"

function init_variables() {
    readonly RESOURCES_DIR="${CURRENT_DIR}/resources"
    readonly POSTPROCESS_DIR="/usr/lib/hailo-post-processes"
    readonly DEFAULT_POSTPROCESS_SO="$POSTPROCESS_DIR/libyolo_post.so"
    readonly DEFAULT_NETWORK_NAME="yolov5"
    readonly DEFAULT_VIDEO_SOURCE="/dev/video0"
    readonly DEFAULT_HEF_PATH="${RESOURCES_DIR}/yolov5m_wo_spp_60p_nv12_fhd.hef"
    readonly DEFAULT_JSON_CONFIG_PATH="$RESOURCES_DIR/configs/yolov5.json"
    readonly DEFAULT_FRONTEND_CONFIG_FILE_PATH="$RESOURCES_DIR/configs/frontend_config.json"
    readonly DEFAULT_ENCODER_CONFIG_PATH="$RESOURCES_DIR/configs/encoder_config.json"
    readonly DEFAULT_UDP_PORT=5000
    readonly DEFAULT_UDP_HOST_IP="10.0.0.2"
    readonly DEFAULT_FRAMERATE="30/1"
    readonly DEFAULT_BITRATE=25000000

    encoder_config_path=$DEFAULT_ENCODER_CONFIG_PATH
    postprocess_so=$DEFAULT_POSTPROCESS_SO
    network_name=$DEFAULT_NETWORK_NAME
    hef_path=$DEFAULT_HEF_PATH
    json_config_path=$DEFAULT_JSON_CONFIG_PATH
    frontend_config_file_path=$DEFAULT_FRONTEND_CONFIG_FILE_PATH
    udp_port=$DEFAULT_UDP_PORT
    udp_host_ip=$DEFAULT_UDP_HOST_IP
    sync_pipeline=false

    framerate=$DEFAULT_FRAMERATE
    max_buffers_size=5

    bitrate=$DEFAULT_BITRATE
    encoding_hrd="hrd=false"

    print_gst_launch_only=false
    additional_parameters=""

    # Limit the encoding bitrate to 20Mbps to support weak host.
    # if you encounter a large latency in the host side.
    # Set the following values down in the encoder config file, to reach the desired latency (will decrease the video quality).
    # ----------------------------------------------
    # bitrate=20000000
    # hrd: true
    # hrd-cpb-size: <same as bitrate>
    # ----------------------------------------------

    mode="daylight"
    tuning_extension=""
    project="hailo15h"
}

function print_usage() {
    echo "Hailo15 Detection pipeline usage:"
    echo ""
    echo "Options:"
    echo "  --help                      Show this help"
    echo "  --show-fps                  Print fps"
    echo "  --print-gst-launch          Print gst-launch command without running"
    echo "  --mode                      mode (e.g., daylight)"
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
                echo "Printing fps"
                additional_parameters="-v | grep hailo_display"
                ;;
            --mode)
                mode="$2"
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

UDP_SINK="udpsink host=$udp_host_ip port=$udp_port"

PIPELINE="gst-launch-1.0 \
    hailofrontendbinsrc config-file-path=$frontend_config_file_path name=frontend \
    frontend. ! \
    queue leaky=no max-size-buffers=$max_buffers_size max-size-bytes=0 max-size-time=0 ! \
    hailonet hef-path=$hef_path scheduling-algorithm=1 vdevice-group-id=device0 ! \
    queue leaky=no max-size-buffers=$max_buffers_size max-size-bytes=0 max-size-time=0 ! \
    hailofilter function-name=$network_name config-path=$json_config_path so-path=$postprocess_so qos=false ! \
    queue leaky=no max-size-buffers=$max_buffers_size max-size-bytes=0 max-size-time=0 ! \
    hailooverlay qos=false ! \
    queue leaky=no max-size-buffers=$max_buffers_size max-size-bytes=0 max-size-time=0 ! \
    hailoencodebin config-file-path=$encoder_config_path ! h264parse config-interval=-1 ! \
    video/x-h264,framerate=$framerate ! \
    tee name=udp_tee \
    udp_tee. ! \
        queue leaky=no max-size-buffers=$max_buffers_size max-size-bytes=0 max-size-time=0 ! \
        rtph264pay ! 'application/x-rtp, media=(string)video, encoding-name=(string)H264' ! \
        $UDP_SINK name=udp_sink sync=$sync_pipeline \
    udp_tee. ! \
        queue leaky=no max-size-buffers=$max_buffers_size max-size-bytes=0 max-size-time=0 ! \
        fpsdisplaysink fps-update-interval=2000 video-sink=fakesink name=hailo_display sync=$sync_pipeline text-overlay=false \
    ${additional_parameters}"

/home/root/apps/clean_symlinks_config_isp.sh --mode "$mode" --tuning "$tuning_extension" --project "$project"
if [ $? -ne 0 ]; then
    echo "Failed to clean symlinks and copy ISP configuration files."
    exit 1
fi

echo "Running $network_name with MODE=${mode:-default}, TUNING_EXTENSION=${tuning_extension:-none}", PROJECT=${project:-hailo15h}
echo "${PIPELINE}"

if [ "$print_gst_launch_only" = true ]; then
    exit 0
fi

eval ${PIPELINE}
