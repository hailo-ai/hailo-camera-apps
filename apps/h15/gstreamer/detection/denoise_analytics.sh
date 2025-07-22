#!/bin/bash
set -e

CURRENT_DIR="$(dirname "$(realpath "${BASH_SOURCE[0]}")")"

function init_variables() {
    readonly RESOURCES_DIR="${CURRENT_DIR}/resources"
    readonly POSTPROCESS_DIR="/usr/lib/hailo-post-processes"
    readonly DEFAULT_POSTPROCESS_SO="$POSTPROCESS_DIR/libyolo_hailortpp_post.so"
    readonly DEFAULT_NETWORK_NAME="yolov8s"
    readonly DEFAULT_VIDEO_SOURCE="/dev/video0"
    readonly DEFAULT_HEF_PATH="${RESOURCES_DIR}/yolov8s.hef"
    readonly DEFAULT_FRONTEND_CONFIG_FILE_PATH="$RESOURCES_DIR/configs/denoise_analytics_frontend_config.json"
    readonly DEFAULT_ENCODER_CONFIG_FILE_PATH="$RESOURCES_DIR/configs/denoise_analytics_encoder_config.json"
    readonly DEFAULT_UDP_PORT=5000
    readonly DEFAULT_UDP_HOST_IP="10.0.0.2"
    readonly DEFAULT_FRAMERATE="30/1"

    postprocess_so=$DEFAULT_POSTPROCESS_SO
    network_name=$DEFAULT_NETWORK_NAME
    input_source=$DEFAULT_VIDEO_SOURCE
    hef_path=$DEFAULT_HEF_PATH
    frontend_config_file_path=$DEFAULT_FRONTEND_CONFIG_FILE_PATH
    encoder_config_file_path=$DEFAULT_ENCODER_CONFIG_FILE_PATH
    udp_port=$DEFAULT_UDP_PORT
    udp_host_ip=$DEFAULT_UDP_HOST_IP
    sync_pipeline=false

    framerate=$DEFAULT_FRAMERATE
    max_buffers_size=3

    encoding_hrd="hrd=false"

    print_gst_launch_only=false
    additional_parameters=""

    mode="daylight"
    tuning_extension=""
    project="hailo15h"
}

function print_usage() {
    echo "Hailo15 Detection pipeline usage:"
    echo ""
    echo "Options:"
    echo "  --help                       Show this help"
    echo "  --network NETWORK            Set network to use. Choose from [yolov5m, yolov8s], default is yolov5m"
    echo "  -i INPUT, --input INPUT      Set the camera source (default $input_source)"
    echo "  --show-fps                   Print fps"
    echo "  --print-gst-launch           Print gst-launch command without running it"
    echo "  --denoise-config-file-path   Set denoise config file path (default ${DEFAULT_FRONTEND_CONFIG_FILE_PATH})"
    echo "  --mode                       mode (e.g., daylight)"
    echo "  --tuning                     tuning extension - relevant only for denoise (e.g., _r0225)"
    echo "  --project                    project name (e.g., hailo15h)"
    exit 0
}

function parse_args() {
    while test $# -gt 0; do
        case "$1" in
            --help|-h)
                print_usage
                ;;
            --network)
                if [ "$2" == "yolov8s" ]; then
                    network_name="yolov8s"
                    hef_path="$RESOURCES_DIR/yolov8s.hef"
                elif [ "$2" != "yolov5m" ]; then
                    echo "Received invalid network: $2"
                    print_usage
                fi
                shift
                ;;
            --print-gst-launch)
                print_gst_launch_only=true
                ;;
            --show-fps)
                echo "Printing fps"
                additional_parameters="-v | grep hailo_display"
                ;;
            -i|--input)
                input_source="$2"
                shift
                ;;
            --denoise-config-file-path)
                frontend_config_file_path="$2"
                shift
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
    hailofrontendbinsrc config-file-path=$frontend_config_file_path name=frontend hailomuxer name=mux \
    frontend. ! \
        queue leaky=no max-size-buffers=$max_buffers_size max-size-bytes=0 max-size-time=0 ! \
        video/x-raw,format=NV12,width=3840,height=2160,framerate=$framerate ! \
    mux. \
    frontend. ! \
        queue leaky=no max-size-buffers=$max_buffers_size max-size-bytes=0 max-size-time=0 ! \
        video/x-raw,format=NV12,width=1280,height=720 ! \
        hailovideoscale use-letterbox=true ! \
        video/x-raw,format=NV12,width=640,height=640 ! \
        hailonet hef-path=$hef_path nms-iou-threshold=0.45 nms-score-threshold=0.3 scheduling-algorithm=1 vdevice-group-id=device0 ! \
        queue leaky=no max-size-buffers=$max_buffers_size max-size-bytes=0 max-size-time=0 ! \
        hailofilter function-name=$network_name so-path=$postprocess_so qos=false ! \
        queue leaky=no max-size-buffers=$max_buffers_size max-size-bytes=0 max-size-time=0 ! \
    mux. \
    mux. ! \
    queue leaky=no max-size-buffers=$max_buffers_size max-size-bytes=0 max-size-time=0 ! \
    hailooverlay qos=false ! \
    queue leaky=no max-size-buffers=$max_buffers_size max-size-bytes=0 max-size-time=0 ! \
    hailoencodebin config-file-path=$encoder_config_file_path ! h264parse config-interval=-1 ! \
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
echo ${PIPELINE}

if [ "$print_gst_launch_only" = true ]; then
    exit 0
fi

eval ${PIPELINE}
