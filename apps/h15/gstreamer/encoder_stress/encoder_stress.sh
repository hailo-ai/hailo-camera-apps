#!/bin/bash
set -e

CURRENT_DIR="$(dirname "$(realpath "${BASH_SOURCE[0]}")")"

function init_variables() {
    print_help_if_needed $@

    # Basic Directories
    readonly POSTPROCESS_DIR="/usr/lib/hailo-post-processes"
    readonly CROPPING_ALGORITHMS_DIR="$POSTPROCESS_DIR/cropping_algorithms"
    readonly RESOURCES_DIR="${CURRENT_DIR}/resources"

    # Default Video
    readonly DEFAULT_VIDEO_SOURCE="/dev/video0"

    readonly FIVE_MP_BITRATE=25000000
    readonly HD_BITRATE=6000000
    readonly SD_BITRATE=3000000

    readonly DEFAULT_MAX_BUFFER_SIZE=5
    readonly DEFAULT_FORMAT="NV12"
    readonly DEFAULT_FRONTEND_CONFIG_FILE_PATH="$RESOURCES_DIR/configs/frontend_config_5mp.json"

    input_source=$DEFAULT_VIDEO_SOURCE

    json_config_path_max_sensor_res="$RESOURCES_DIR/configs/encoder_config_sink_5mp.json"
    json_config_path_max_sensor_res_15_fps="$RESOURCES_DIR/configs/encoder_config_sink_5mp_15fps.json"
    json_config_path_fhd="$RESOURCES_DIR/configs/encoder_config_sink_fhd.json"
    
    frontend_config_file_path="$DEFAULT_FRONTEND_CONFIG_FILE_PATH"

    encoding_hrd="hrd=false"

    # Limit the encoding bitrate to 10Mbps to support weak host.
    # if you encounter a large latency in the host side.
    # Set the following values down in the encoder config file, to reach the desired latency (will decrease the video quality).
    # ----------------------------------------------
    # bitrate=10000000
    # hrd=true
    # hrd-cpb-size=<same as bitrate>
    # ----------------------------------------------

    max_buffers_size=$DEFAULT_MAX_BUFFER_SIZE

    print_gst_launch_only=false
    additonal_parameters=""
    video_format=$DEFAULT_FORMAT
    sync_pipeline=false
    project="hailo15l"

    mode="daylight"
    tuning_extension=""
    lens="theia_sl410m"
}

function print_help_if_needed() {
    while test $# -gt 0; do
        if [ "$1" = "--help" ] || [ "$1" == "-h" ]; then
            print_usage
        fi
        shift
    done
}

function print_usage() {
    echo "Encoder stress streaming pipeline usage:"
    echo ""
    echo "Options:"
    echo "  -h --help                  Show this help"
    echo "  --show-fps                 Print fps"
    echo "  --print-gst-launch         Print the ready gst-launch command without running it"
    echo "  -i --input-source          Set the input source (default $DEFAULT_VIDEO_SOURCE)"
    echo "  --vision-config-file-path  Set the frontend config file path (default $DEFAULT_FRONTEND_CONFIG_FILE_PATH)"
    echo "  --project                  Set the project name (default hailo15l)"
    echo "  --mode                     mode (e.g., daylight)"
    echo "  --tuning                   tuning extension - relevant only for denoise (e.g., _r0225)"
    echo "  --lens                     lens name (default theia_sl410m)"
    exit 0
}

function parse_args() {
    while test $# -gt 0; do
        if [ "$1" = "--print-gst-launch" ]; then
            print_gst_launch_only=true
        elif [ "$1" = "--show-fps" ]; then
            echo "Printing fps"
            additonal_parameters="-v | grep -e hailo_display"
        elif [ "$1" = "-i" ] || [ "$1" = "--input-source" ]; then
            input_source="$2"
            shift
        elif [ "$1" = "--vision-config-file-path" ]; then
            frontend_config_file_path="$2"
            shift
        elif [ "$1" = "--mode" ]; then
            mode="$2"
            shift
        elif [ "$1" = "--tuning" ]; then
            tuning_extension="$2"
            shift
        elif [ "$1" = "--lens" ]; then
            lens="$2"
            shift
        elif [ "$1" = "--project" ]; then
            project="$2"
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

function create_pipeline() {

    FPS_DISP="fpsdisplaysink fps-update-interval=2000 text-overlay=false sync=$sync_pipeline video-sink=fakesink"

    UDP_SINK="queue leaky=no max-size-buffers=$max_buffers_size max-size-bytes=0 max-size-time=0 ! \
              rtph264pay ! 'application/x-rtp, media=(string)video, encoding-name=(string)H264' ! \
              udpsink host=10.0.0.2 sync=$sync_pipeline"

    FIVE_MP_BRANCH="queue leaky=no max-size-buffers=$max_buffers_size max-size-bytes=0 max-size-time=0 ! \
                    hailoencodebin config-file-path=$json_config_path_max_sensor_res ! \
                    video/x-h264 ! \
                    tee name=five_mp_enc_tee \
                    five_mp_enc_tee. ! \
                        $UDP_SINK port=5000 \
                    five_mp_enc_tee. ! \
                        queue leaky=no max-size-buffers=$max_buffers_size max-size-bytes=0 max-size-time=0 ! \
                        $FPS_DISP name=hailo_display_5mp_enc "

    FIVE_MP2_BRANCH="queue leaky=no max-size-buffers=$max_buffers_size max-size-bytes=0 max-size-time=0 ! \
                    hailoencodebin config-file-path=$json_config_path_max_sensor_res ! \
                    video/x-h264 ! \
                    tee name=five_mp_two_enc_tee \
                    five_mp_two_enc_tee. ! \
                        $UDP_SINK port=5001 \
                    five_mp_two_enc_tee. ! \
                        queue leaky=no max-size-buffers=$max_buffers_size max-size-bytes=0 max-size-time=0 ! \
                        $FPS_DISP name=hailo_display_5mp_two_enc "

    FIVE_MP_15_BRANCH="queue leaky=no max-size-buffers=$max_buffers_size max-size-bytes=0 max-size-time=0 ! \
                    hailoencodebin config-file-path=$json_config_path_max_sensor_res_15_fps ! \
                    video/x-h264 ! \
                    tee name=five_mp_15_enc_tee \
                    five_mp_15_enc_tee. ! \
                        $UDP_SINK port=5002 \
                    five_mp_15_enc_tee. ! \
                        queue leaky=no max-size-buffers=$max_buffers_size max-size-bytes=0 max-size-time=0 ! \
                        $FPS_DISP name=hailo_display_5mp_15_enc "

    FHD_BRANCH="queue leaky=no max-size-buffers=$max_buffers_size max-size-bytes=0 max-size-time=0 ! \
                hailoencodebin config-file-path=$json_config_path_fhd ! \
                video/x-h264 ! \
                tee name=fhd_tee \
                fhd_tee. ! \
                    $UDP_SINK port=5003 \
                fhd_tee. ! \
                    queue leaky=no max-size-buffers=$max_buffers_size max-size-bytes=0 max-size-time=0 ! \
                    $FPS_DISP name=hailo_display_fhd_enc "
}

/home/root/apps/clean_symlinks_config_isp.sh --mode "$mode" --tuning "$tuning_extension" --lens "$lens" --project "$project"
if [ $? -ne 0 ]; then
    echo "Failed to clean symlinks and copy ISP configuration files."
    exit 1
fi

create_pipeline $@

PIPELINE="${debug_stats_export} gst-launch-1.0 \
    hailofrontendbinsrc config-file-path=$frontend_config_file_path name=preproc \
    preproc. ! $FIVE_MP_BRANCH \
    preproc. ! $FIVE_MP2_BRANCH \
    preproc. ! $FIVE_MP_15_BRANCH \
    preproc. ! $FHD_BRANCH \
    ${additonal_parameters} "

echo "Running Pipeline..."
echo ${PIPELINE}

if [ "$print_gst_launch_only" = true ]; then
    exit 0
fi

eval ${PIPELINE}
