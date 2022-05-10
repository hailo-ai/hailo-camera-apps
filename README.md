# Hailo TAPPAS - Optimized Execution of Video-Processing Pipelines

<div align="center"><img width="600px" height="300px" src="./resources/TAPPAS.png"/></div>

<br>

<div align="center">
<img src="./apps/gstreamer/x86/cascading_networks/readme_resources/cascading_app.gif"/>
</div>

---

## Overview

TAPPAS is a framework for optimized execution of video-processing pipelines in systems with the Hailo-8 accelerator. Hailo-8 accelerates NN inference. TAPPAS optimizes the rest of the data path running on the host.

The core of TAPPAS is a library of non-neural video-processing elements.
These elements include:

- Hailo abstraction - elements to send/receive data from the Hailo accelerator.
- Glue-logic (e.g. format conversion YUV->RGB )
- Decision logic (e.g.rule-based filtering; branching; conditional invocation etc.)
- Data flow regulation elements (e.g. buffering; throttling; muxing/de-muxing)
- Functional elements (e.g. NMS, Tracking, Tiling, etc. )

TAPPAS comes packaged with a rich set of example applications built on top of TAPPAS. These examples demonstrate common use-cases and showcase performance.

<div align="center"><img src="resources/HAILO_TAPPAS_SW_STACK.JPG"/></div>

---

## Getting started

### Prerequisites

- Hailo-8 device
- HailoRT PCIe driver installed
- At least 6GB's of free disk space

> Note: This version runs and is tested with HailoRT version 4.7.0.

### Installation

| Option                      |                           Instructions                           |                           Supported OS                            |
| --------------------------- | :--------------------------------------------------------------: | :---------------------------------------------------------------: |
| **Hailo SW Suite*** [^1]    | [SW Suite Install guide](docs/installation/sw-suite-install.md)  |         Ubuntu x86 18.04, Ubuntu x86 20.04, Ubuntu 20.04          |
| Pre-built Docker image [^1] |   [Docker install guide](docs/installation/docker-install.md)    | Ubuntu x86 18.04, Ubuntu x86 20.04, Ubuntu aarch64 20.04 (64-bit) |
| Manual install [^2]         |   [Manual install guide](docs/installation/manual-install.md)    |     Ubuntu x86 18.04, Ubuntu x86 20.04, Ubuntu aarch64 20.04      |
| Yocto installation [^2]     | [Read more about Yocto installation](docs/installation/yocto.md) |                       Yocto supported BSP's                       |
| Offline Install [^1]        |         [Offline install guide](docs/offline-install.md)         |                  Same as: Pre-built Docker image                  |

* It is recommended to start your development journey by first installing the Hailo SW Suite
[^1]: Requires [hailo.ai](https://hailo.ai/developer-zone/) login and download.
[^2]: Can be installed directly from the [TAPPAS GitHub](https://github.com/hailo-ai/tappas)

### Documentation

- [Framework architecture and elements documentation](docs/TAPPAS_architecture.md)
- [Guide to writing your own C++ postprocess element](docs/write_your_own_application/write-your-own-postprocess.md)
- [Guide to writing your own Python postprocess element](docs/write_your_own_application/write-your-own-python-postprocess.md)
- [Debugging and profiling performance](docs/write_your_own_application/debugging.md)
- [Cross compile](tools/cross_compiler/README.md) - A guide for cross-compiling

---

## Example Applications built with TAPPAS

TAPPAS comes with a rich set of pre-configured pipelines optimized for different common hosts.

> **NOTE:** The x86 application examples can be run on various processor architectures, since these examples are built intentionally without the use of hardware accelerators.

> **NOTE:** Architecture-specific app examples (i.MX, Raspberry PI, etc..) use platform-specific hardware accelerators and are not compatible with different architectures.

### Basic Single Network Pipelines

Pipelines that run a single network. The diagram below shows the pipeline data-flow.
<div align="left"><img src="resources/single_net_pipeline.jpg"/></div>

The following table details the currently availble examples.

|                       | [x86](apps/gstreamer/x86/README.md) | [iMX8](apps/gstreamer/imx/README.md) | [Raspberry Pi 4](apps/gstreamer/raspberrypi/README.md) |
| :-------------------: | :---------------------------------: | :----------------------------------: | :----------------------------------------------------: |
|   Object Detection    |         :heavy_check_mark:          |          :heavy_check_mark:          |                   :heavy_check_mark:                   |
|    Pose Estimation    |         :heavy_check_mark:          |                                      |                   :heavy_check_mark:                   |
| Semantic Segmentation |         :heavy_check_mark:          |                                      |                                                        |
|   Depth Estimation    |         :heavy_check_mark:          |                                      |                   :heavy_check_mark:                   |
|    Face Detection     |         :heavy_check_mark:          |                                      |                   :heavy_check_mark:                   |
|    Facial landmark    |         :heavy_check_mark:          |          :heavy_check_mark:          |                                                        |
|  HD Object Detection  |         :heavy_check_mark:          |                                      |                                                        |
| Instance segmentation |         :heavy_check_mark:          |                                      |                                                        |

### Two Network Pipelines

Examples of basic pipelines running two networks.
The parallel networks pipeline is an simple extension of the single network pipeline and is shown in the following diagram:
<div align="left"><img src="resources/parallel_nets_pipeline.png"/></div>

The cascaded (serial) flow shows two networks running in series. This example pipeline is of the popular configuration where the first network is a detector which finds some Region-of-Interest (ROI) in the input image and the second network processes the cropped ROI (a face-detection-and-landmarking use case of this pipeline is shown at the top of this guide). The pipeline is shown in the following diagram:
<div align="left"><img src="resources/cascaded_nets_pipeline.png"/></div>

|                                          | [x86](apps/gstreamer/x86/README.md) | [iMX8](apps/gstreamer/imx/README.md) | [Raspberry Pi 4](apps/gstreamer/raspberrypi/README.md) |
| :--------------------------------------: | :---------------------------------: | :----------------------------------: | :----------------------------------------------------: |
| Parallel - Object Det + Depth Estimation |         :heavy_check_mark:          |                                      |                   :heavy_check_mark:                   |
| Parallel - Object Det + Pose Estimation  |         :heavy_check_mark:          |                                      |                                                        |
|  Cascaded  - Face Detection & Landmarks  |         :heavy_check_mark:          |                                      |                   :heavy_check_mark:                   |

### Multi-Stream Pipelines

<div align="left"><img src="docs/resources/one_network_multi_stream.png"/></div>


|                                            | [x86](apps/gstreamer/x86/README.md) | [iMX8](apps/gstreamer/imx/README.md) | [Raspberry Pi 4](apps/gstreamer/raspberrypi/README.md) |
| :----------------------------------------: | :---------------------------------: | :----------------------------------: | :----------------------------------------------------: |
|       Multi-stream Object Detection        |         :heavy_check_mark:          |                                      |                                                        |
| Multi-stream Multi-Device Object Detection |         :heavy_check_mark:          |                                      |

### Pipelines for High-Resolution Processing Via Tiling

<div align="left"><img src="docs/resources/tiling-example.png"/></div>


|                     | [x86](apps/gstreamer/x86/README.md) | [iMX8](apps/gstreamer/imx/README.md) | [Raspberry Pi 4](apps/gstreamer/raspberrypi/README.md) |
| :-----------------: | :---------------------------------: | :----------------------------------: | :----------------------------------------------------: |
| HD Object Detection |         :heavy_check_mark:          |                                      |                                                        |

### Example Use Case Pipelines

Our LPR application demonstrates the use of 3 networks, with a database.
The pipeline demonstrates inference based decision making (Vehicle detection) for secondary inference tasks (License plate extraction). This allows multiple networks to cooperate in the pipeline for reactive behavior.

<div align="left"><img src="resources/lpr_pipeline.png"/></div>

|       | [x86](apps/gstreamer/x86/README.md) | [iMX8](apps/gstreamer/imx/README.md) |
| :---: | :---------------------------------: | :----------------------------------: |
|  LPR  |         :heavy_check_mark:          |          :heavy_check_mark:          |

---

## Changelog

<details>
<summary> v3.18.0 (April 2022) </summary>

- New Apps:
  - LPR (License Plate Recognition) pipeline and facial landmark pipeline for [i.MX Pipelines](apps/gstreamer/imx/README.md)
- Added the ability of compiling a specific TAPPAS target (post-processes, elements)
- Improved the performance of Raspberry Pi example applications

</details>

<details>
<summary> v3.17.0 (March 2022) </summary>

- New Apps:
  - LPR (License Plate Recognition) pipeline for [x86 Pipelines](apps/gstreamer/x86/README.md) (preview)
  - Detection & pose estimation app
  - Detection (MobilenetSSD) - Multi scale tiling app
- Update infrastructure to use new HailoRT installation packages
- Code is now publicly available on  [Github](https://github.com/hailo-ai/tappas)

</details>
<details>
<summary> v3.16.0 (March 2022) </summary>

- New Apps:
  - Hailo [Century](https://hailo.ai/product-hailo/hailo-8-century-evaluation-platform/) app - Demonstrates detection on one video file source over 6 different Hailo-8 devices
  - Python app - A classification app using a post-process written in Python
- New Elements:
  - Tracking element "HailoTracker" - Add tracking capabilities
  - Python element "HailoPyFilter" - Enables to write post-processes using Python
- Yocto Hardknott is now supported
- Raspberry Pi 4 Ubuntu dedicated apps
- HailoCropper cropping bug fixes
- HailoCropper now accepts cropping method as a shared object (.so)

</details>

<details>
<summary> v3.14.1 (March 2022) </summary>

- Fix Yocto Gatesgarth compilation issue
- Added support for hosts without X-Video adapter

</details>

<details>
<summary> v3.15.0 (February 2022) </summary>

- New Apps:
  - Detection and depth estimation - Networks switch app
  - Detection (MobilenetSSD) - Single scale tilling app

</details>

<details>
<summary> v3.14.0 (January 2022) </summary>

- New Apps:
  - Cascading apps - Face detection and then facial landmarking
- New Yocto layer - Meta-hailo-tappas
- Window enlargement is now supported
- Added the ability to run on multiple devices
- Improved latency on Multi-device RTSP app

</details>

<details>
<summary> v3.13.0 (November 2021) </summary>

- Context switch networks in multi-stream apps are now supported
- New Apps:
  - Yolact - Instance segmentation
  - FastDepth - Depth estimation
  - Two networks in parallel on the same device - FastDepth + Mobilenet SSD
  - Retinaface
- Control Element Integration - Displaying device stats inside a GStreamer pipeline (Power, Temperature)
- New Yocto recipes - Compiling our GStreamer plugins is now available as a Yocto recipe
- Added a C++ detection example (native C++ example for writing an app, without GStreamer)

</details>

<details>
<summary> v3.12.0 (October 2021) </summary>

- Detection app - MobilenetSSD added
- NVR multi-stream multi device app (detection and pose estimation)
- Facial Landmarks app
- Segmentation app
- Classification app
- Face detection app
- Hailomuxer gstreamer element
- Postprocess implementations for various networks
- GStreamer infrastructure improvements
- Added ARM architecture support and documentation

</details>

<details>
<summary> v3.11.0 (September 2021) </summary>

- GStreamer based initial release
- NVR multi-stream detection app
- Detection app
- Hailofilter gstreamer element
- Pose Estimation app

</details>.
