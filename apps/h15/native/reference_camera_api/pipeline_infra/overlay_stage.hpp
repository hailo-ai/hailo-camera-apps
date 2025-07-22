#pragma once
#include "stage.hpp"
#include "buffer.hpp"
#include "queue.hpp"
#include "hailo_objects.hpp"

#include "overlay_native.hpp"
#include <unordered_set>

/**
 * @struct HailoOverlay
 * @brief Structure containing configuration parameters for overlay.
 */
struct HailoOverlay
{
    int line_thickness;          /**< Line thickness for overlay. */
    int font_thickness;          /**< Font thickness for overlay. */
    float landmark_point_radius; /**< Radius for landmark points. */
    bool face_blur;              /**< Enable or disable face blur. */
    bool show_confidence;        /**< Enable or disable confidence display. */
    bool local_gallery;          /**< Enable or disable local gallery usage. */
    uint mask_overlay_n_threads; /**< Number of threads for mask overlay. */
};

/**
 * @class OverlayStage
 * @brief Class responsible for handling overlay stage that will do the drawing.
 */
class OverlayStage : public ConnectedStage
{
  private:
    HailoOverlay m_hailooverlay_info;            /**< Overlay configuration parameters. */
    std::atomic_bool m_skip;                     /**< Flag to skip drawing. */
    bool m_partial_landmarks;                    /**< Flag to enable partial landmarks. */
    size_t m_min_landmark;                       /**< Minimum landmark index. */
    size_t m_max_landmark;                       /**< Maximum landmark index. */
    std::unordered_set<int> m_class_ids_to_draw; /**< Label for the overlay stage. */
    std::function<cv::Scalar(const HailoDetectionPtr &)>
        m_color_selector; /**< Function to select color based on detection. */

  public:
    /**
     * @brief Constructor for OverlayStage.
     * @param name The name of the stage.
     * @param queue_size Size of the queue for this stage.
     * @param leaky Indicates if the queue is leaky.
     * @param print_fps Flag to enable or disable printing FPS information.
     */
    OverlayStage(std::string name, bool skip = false, bool partial_landmarks = false, size_t min_landmark = 0,
                 size_t max_landmark = 0, size_t queue_size = 5, bool leaky = false,
                 std::unordered_set<int> class_ids_to_draw = {},
                 std::function<cv::Scalar(const HailoDetectionPtr &)> color_selector = nullptr, bool print_fps = false)
        : ConnectedStage(name, queue_size, leaky, print_fps), m_skip(skip), m_partial_landmarks(partial_landmarks),
          m_min_landmark(min_landmark), m_max_landmark(max_landmark), m_class_ids_to_draw(std::move(class_ids_to_draw)),
          m_color_selector(color_selector)
    {
    }

    /**
     * @brief Initialize the overlay stage.
     * @return Status of the initialization.
     */
    AppStatus init() override
    {
        /* Set overlay default values */
        m_hailooverlay_info.line_thickness = 1;
        m_hailooverlay_info.font_thickness = 1;
        m_hailooverlay_info.face_blur = false;
        m_hailooverlay_info.show_confidence = true;
        m_hailooverlay_info.local_gallery = false;
        m_hailooverlay_info.landmark_point_radius = 3;
        m_hailooverlay_info.mask_overlay_n_threads = 0;
        m_debug_counters = std::make_shared<OverlayCounters>(m_stage_name);
        return AppStatus::SUCCESS;
    }

    /**
     * @brief Deinitialize the overlay stage.
     * @return Status of the deinitialization.
     */
    AppStatus deinit() override
    {
        return AppStatus::SUCCESS;
    }

    /**
     * @brief Process the given data buffer and apply overlay.
     * @param data The data buffer to process.
     * @return Status of the processing.
     */
    AppStatus process(BufferPtr data)
    {
        std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();

        if (m_skip)
        {
            data->add_time_stamp(m_stage_name);
            set_duration(data);
            send_to_subscribers(data);
            return AppStatus::SUCCESS;
        }
        m_debug_counters->increment_input_frames();
        std::shared_ptr<HailoMat> hmat = std::make_shared<HailoNV12Mat>(
            (uint8_t *)data->get_buffer()->get_plane_ptr(0), data->get_buffer()->buffer_data->height,
            data->get_buffer()->buffer_data->width, data->get_buffer()->get_plane_stride(0),
            data->get_buffer()->get_plane_stride(1), m_hailooverlay_info.line_thickness,
            m_hailooverlay_info.font_thickness, (uint8_t *)data->get_buffer()->get_plane_ptr(0),
            (uint8_t *)data->get_buffer()->get_plane_ptr(1));

        if (hmat)
        {
            auto detections = hailo_common::get_hailo_detections(data->get_roi());
            if (!detections.empty())
            {
                std::unordered_map<std::string, int> label_count;
                for (const auto &detection : detections)
                    ++label_count[detection->get_label()];

                std::ostringstream labels_stream;
                for (const auto &[label, count] : label_count)
                    labels_stream << (labels_stream.tellp() ? ", " : "") << label << ": " << count;

                REFERENCE_CAMERA_LOG_TRACE("Overlay stage: Processing frame with {}", labels_stream.str());
            }

            if (DmaMemoryAllocator::get_instance().dmabuf_sync_start(data->get_buffer()->get_plane_ptr(0)) !=
                MEDIA_LIBRARY_SUCCESS)
                return AppStatus::DMA_ERROR;
            if (DmaMemoryAllocator::get_instance().dmabuf_sync_start(data->get_buffer()->get_plane_ptr(1)) !=
                MEDIA_LIBRARY_SUCCESS)
                return AppStatus::DMA_ERROR;
            // Blur faces if face-blur is activated.
            if (m_hailooverlay_info.face_blur)
            {
                face_blur(*hmat.get(), data->get_roi());
            }
            // Draw all results of the given roi on mat.
            overlay_status_t ret =
                draw_all(*hmat.get(), data->get_roi(), m_debug_counters, m_hailooverlay_info.landmark_point_radius,
                         m_hailooverlay_info.show_confidence, m_hailooverlay_info.local_gallery,
                         m_hailooverlay_info.mask_overlay_n_threads, m_partial_landmarks, m_min_landmark,
                         m_max_landmark, m_class_ids_to_draw, m_color_selector);
            if (ret != OVERLAY_STATUS_OK)
            {
                std::cerr << " Overlay failure draw_all failed, status = " << ret << std::endl;
                REFERENCE_CAMERA_LOG_ERROR("Overlay failure draw_all failed, status = {}", ret);
            }
            if (DmaMemoryAllocator::get_instance().dmabuf_sync_end(data->get_buffer()->get_plane_ptr(0)) !=
                MEDIA_LIBRARY_SUCCESS)
                return AppStatus::DMA_ERROR;
            if (DmaMemoryAllocator::get_instance().dmabuf_sync_end(data->get_buffer()->get_plane_ptr(1)) !=
                MEDIA_LIBRARY_SUCCESS)
                return AppStatus::DMA_ERROR;
        }
        std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
        if (m_print_fps)
        {
            std::cout << "Overlay time = " << std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count()
                      << "[microseconds]" << std::endl;
        }
        REFERENCE_CAMERA_LOG_DEBUG("Overlay time = {}[microseconds]",
                                   std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count());
        data->add_time_stamp(m_stage_name);
        set_duration(data);
        m_debug_counters->increment_output_frames();
        send_to_subscribers(data);

        return AppStatus::SUCCESS;
    }

    /**
     * @brief Set the overlay skip flag.
     * @param skip Flag to set the skip state.
     */
    void set_skip(bool skip)
    {
        m_skip = skip;
    }

    /**
     * @brief Get the overlay skip flag.
     * @return Current skip state.
     */
    bool get_skip()
    {
        return m_skip;
    }
};

class OverlayStageBuild : public OverlayStage
{
  public:
    class Builder
    {

      private:
        std::optional<std::string> m_stage_name;
        bool m_skip = false;
        bool m_partial_landmarks = false;
        size_t m_min_landmark = 0;
        size_t m_max_landmark = 0;
        size_t m_queue_size = 5;
        bool m_leaky = false;
        bool m_print_fps = false;
        std::unordered_set<int> m_class_ids_to_draw = {};
        std::function<cv::Scalar(const HailoDetectionPtr &)> m_color_selector = nullptr;

      public:
        Builder &set_stage_name(std::string name)
        {
            m_stage_name = name;
            return *this;
        }
        Builder &set_skip_opt(bool skip)
        {
            m_skip = skip;
            return *this;
        }
        Builder &set_partial_landmarks(bool partial_landmarks)
        {
            m_partial_landmarks = partial_landmarks;
            return *this;
        }
        Builder &set_min_landmark(size_t min_landmark)
        {
            m_min_landmark = min_landmark;
            return *this;
        }
        Builder &set_max_landmark(size_t max_landmark)
        {
            m_max_landmark = max_landmark;
            return *this;
        }
        Builder &set_queue_size(size_t size)
        {
            m_queue_size = size;
            return *this;
        }
        Builder &set_leaky_opt(bool activate)
        {
            m_leaky = activate;
            return *this;
        }
        Builder &set_class_ids_to_draw(std::unordered_set<int> class_ids_to_draw)
        {
            m_class_ids_to_draw = class_ids_to_draw;
            return *this;
        }
        Builder &set_color_selector(std::function<cv::Scalar(const HailoDetectionPtr &)> color_selector)
        {
            m_color_selector = color_selector;
            return *this;
        }
        Builder &set_printfps_opt(bool activate)
        {
            m_print_fps = activate;
            return *this;
        }

        std::shared_ptr<OverlayStage> buildptr() const
        {
            THROW_IF_MISSING(m_stage_name.has_value(), "set_stage_name");

            return std::make_shared<OverlayStage>(m_stage_name.value(), m_skip, m_partial_landmarks, m_min_landmark,
                                                  m_max_landmark, m_queue_size, m_leaky, m_class_ids_to_draw,
                                                  m_color_selector, m_print_fps);
        }
    };

    static Builder create()
    {
        return Builder();
    }
};
