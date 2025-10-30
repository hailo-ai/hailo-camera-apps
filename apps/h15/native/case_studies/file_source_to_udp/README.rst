==========================
File Source to UDP Case Study
==========================

Overview
========

This case study demonstrates a minimal pipeline that reads video from a file and streams it over UDP. The pipeline consists of three stages:

.. code-block:: text

    FileSourceStage -> EncoderStage -> UdpStage

Usage
=====

Required Parameters
------------------

The following parameters are required for all runs:

- ``-f, --file`` - Video file path (NV12 format)
- ``-w, --width`` - Video width in pixels  
- ``-g, --height`` - Video height in pixels
- ``-e, --encoder-config`` - Encoder configuration file path

Optional Parameters  
------------------

- ``-r, --fps`` - Frame rate (default: 30)
- ``-o, --host`` - Target IP address (default: 10.0.0.2)
- ``-p, --port`` - UDP port (default: 5000)
- ``-t, --timeout`` - Runtime timeout in seconds (default: 60)
- ``-s, --print-fps`` - Enable FPS statistics (default: false)

Basic Usage
-----------

.. code-block:: bash

    # Get help
    ./file_source_to_udp_case_study --help

    # Basic usage with required parameters
    ./file_source_to_udp_case_study -f video.nv12 -w 1920 -g 1080 -e encoder.json

Examples
--------

.. code-block:: bash

    # Basic usage with required parameters
    ./file_source_to_udp_case_study -f /path/to/video.nv12 -w 1920 -g 1080 -e /path/to/encoder.json

    # Custom network settings
    ./file_source_to_udp_case_study -f video.nv12 -w 1920 -g 1080 -e encoder.json -o 192.168.1.100 -p 6000

    # Custom resolution and frame rate
    ./file_source_to_udp_case_study -f video.nv12 -w 1280 -g 720 -e encoder.json -r 25

    # Enable FPS monitoring
    ./file_source_to_udp_case_study -f video.nv12 -w 1920 -g 1080 -e encoder.json -s

    # Run for 5 minutes
    ./file_source_to_udp_case_study -f video.nv12 -w 1920 -g 1080 -e encoder.json -t 300

Input Requirements
==================

Video File Format
-----------------

- **Format**: Raw NV12 (YUV 4:2:0)
- **Resolution**: Any resolution (specify with ``-w`` and ``-g``)
- **Frame Rate**: Any frame rate (specify with ``-r``)

Creating Test Video Files
-------------------------

.. code-block:: bash

    # Convert MP4 to NV12 using FFmpeg
    ffmpeg -i input.mp4 -pix_fmt nv12 -f rawvideo output.nv12

    # Create test pattern (colored bars)
    ffmpeg -f lavfi -i testsrc=duration=10:size=1920x1080:rate=30 \
           -pix_fmt nv12 -f rawvideo test_video.nv12

