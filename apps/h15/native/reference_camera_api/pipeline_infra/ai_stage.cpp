#include "ai_stage.hpp"

HailortAsyncStage::HailortAsyncStage(std::string name, std::string hef_path, size_t queue_size, int output_pool_size,
                                     std::string group_id, int batch_size, size_t job_limit, int scheduler_threshold,
                                     bool dynamic_threshold, std::chrono::milliseconds scheduler_timeout,
                                     bool print_fps, StagePoolMode pool_mode, float32_t nms_score_threshold)
    : ConnectedStage(name, queue_size, false, print_fps), m_output_pool_size(output_pool_size), m_hef_path(hef_path),
      m_group_id(group_id), m_batch_size(batch_size), m_scheduler_threshold(scheduler_threshold),
      m_dynamic_threshold(dynamic_threshold), m_nms_score_threshold(nms_score_threshold),
      m_scheduler_timeout(scheduler_timeout), m_jobs_limit(job_limit), m_pool_mode(pool_mode)
{
    m_last_infer_job = nullptr;
    m_active_jobs = 0;
}

AppStatus HailortAsyncStage::init()
{
    hailo_vdevice_params_t vdevice_params = {0};
    hailo_init_vdevice_params(&vdevice_params);
    vdevice_params.group_id = m_group_id.c_str();
    m_debug_counters = std::make_shared<AIStageCounters>(m_stage_name);

    // Create a vdevice
    auto vdevice_exp = hailort::VDevice::create(vdevice_params);
    if (!vdevice_exp)
    {
        std::cerr << "Failed create vdevice, Hailort status = " << vdevice_exp.status() << std::endl;
        REFERENCE_CAMERA_LOG_ERROR("Failed create vdevice, Hailort status = {}", vdevice_exp.status());
        return AppStatus::HAILORT_ERROR;
    }
    m_vdevice = vdevice_exp.release();

    // Create an infer model
    auto infer_model_exp = m_vdevice->create_infer_model(m_hef_path.c_str());
    if (!infer_model_exp)
    {
        std::cerr << "Failed to create infer model, Hailort status = " << infer_model_exp.status() << std::endl;
        REFERENCE_CAMERA_LOG_ERROR("Failed to create infer model, Hailort status = {}", infer_model_exp.status());
        return AppStatus::HAILORT_ERROR;
    }
    m_infer_model = infer_model_exp.release();
    m_infer_model->set_batch_size(m_batch_size);

    for (auto &output : m_infer_model->outputs())
    {
        auto infer_stream = m_infer_model->output(output.name()).expect("Failed to get output tensor");
        if (infer_stream.is_nms() && m_nms_score_threshold > 0.0f)
        {
            infer_stream.set_nms_score_threshold(m_nms_score_threshold);
        }
    }

    // Configure the infer model
    auto configured_infer_model_exp = m_infer_model->configure();
    if (!configured_infer_model_exp)
    {
        std::cerr << "Failed to create configured infer model, Hailort status = " << configured_infer_model_exp.status()
                  << std::endl;
        REFERENCE_CAMERA_LOG_ERROR("Failed to create configured infer model, Hailort status = {}",
                                   configured_infer_model_exp.status());
        return AppStatus::HAILORT_ERROR;
    }
    m_configured_infer_model = configured_infer_model_exp.release();
    m_configured_infer_model.set_scheduler_threshold(m_scheduler_threshold);
    m_configured_infer_model.set_scheduler_timeout(std::chrono::milliseconds(m_scheduler_timeout));

    // Create bindings through which to connect buffers for inference
    auto bindings = m_configured_infer_model.create_bindings();
    if (!bindings)
    {
        std::cerr << "Failed to create infer bindings, Hailort status = " << bindings.status() << std::endl;
        REFERENCE_CAMERA_LOG_ERROR("Failed to create infer bindings, Hailort status = {}", bindings.status());
        return AppStatus::HAILORT_ERROR;
    }
    m_bindings = bindings.release();

    // Prepare a buffer pool for each output tensor
    for (auto &output : m_infer_model->outputs())
    {
        size_t tensor_size = output.get_frame_size();
        std::string tensor_name = m_stage_name + "/" + output.name();
        m_tensor_buffer_pools[output.name()] = std::make_shared<MediaLibraryBufferPool>(
            tensor_size, 1, HAILO_FORMAT_GRAY8, m_output_pool_size, HAILO_MEMORY_TYPE_DMABUF, tensor_size, tensor_name);
        if (m_tensor_buffer_pools[output.name()]->init() != MEDIA_LIBRARY_SUCCESS)
        {
            return AppStatus::BUFFER_ALLOCATION_ERROR;
        }
    }

    // Gather the vstream info for each output tensor
    auto vstream_infos = m_infer_model->hef().get_output_vstream_infos();
    if (!vstream_infos)
    {
        std::cerr << "Failed to get vstream info, Hailort status = " << vstream_infos.status() << std::endl;
        REFERENCE_CAMERA_LOG_ERROR("Failed to get vstream info, Hailort status = {}", vstream_infos.status());
        return AppStatus::HAILORT_ERROR;
    }
    for (const auto &vstream_info : vstream_infos.value())
    {
        m_vstream_infos[vstream_info.name] = vstream_info;
    }

    return AppStatus::SUCCESS;
}

AppStatus HailortAsyncStage::deinit()
{
    // Wait for last infer to finish
    if (m_last_infer_job)
    {
        auto status = m_last_infer_job->wait(std::chrono::milliseconds(10000));
        if (HAILO_SUCCESS != status)
        {
            std::cerr << "Failed to wait for infer to finish, status = " << status << std::endl;
            REFERENCE_CAMERA_LOG_ERROR("Failed to wait for infer to finish, status = {}", status);
            return AppStatus::HAILORT_ERROR;
        }
    }
    for (auto &queue : m_queues)
    {
        queue->flush();
    }

    return AppStatus::SUCCESS;
}

AppStatus HailortAsyncStage::set_pix_buf(const HailoMediaLibraryBufferPtr buffer)
{
    int y_plane_fd = buffer->get_plane_fd(0);
    uint32_t y_plane_size = buffer->get_plane_size(0);

    int uv_plane_fd = buffer->get_plane_fd(1);
    uint32_t uv_plane_size = buffer->get_plane_size(1);

    hailo_pix_buffer_t pix_buffer{};
    pix_buffer.memory_type = HAILO_PIX_BUFFER_MEMORY_TYPE_DMABUF;
    pix_buffer.number_of_planes = 2;
    pix_buffer.planes[0].bytes_used = y_plane_size;
    pix_buffer.planes[0].plane_size = y_plane_size;
    pix_buffer.planes[0].fd = y_plane_fd;

    pix_buffer.planes[1].bytes_used = uv_plane_size;
    pix_buffer.planes[1].plane_size = uv_plane_size;
    pix_buffer.planes[1].fd = uv_plane_fd;

    auto status = m_bindings.input()->set_pix_buffer(pix_buffer);
    if (HAILO_SUCCESS != status)
    {
        std::cerr << "Failed to set infer input buffer, Hailort status = " << status << std::endl;
        REFERENCE_CAMERA_LOG_ERROR("Failed to set infer input buffer, Hailort status = {}", status);
        return AppStatus::HAILORT_ERROR;
    }

    return AppStatus::SUCCESS;
}

AppStatus HailortAsyncStage::acquire_and_set_tensor_buffers(std::unordered_map<std::string, BufferPtr> &tensor_buffers)
{
    // Acquire a buffer for each output tensor
    for (auto &output : m_infer_model->outputs())
    {
        // Acquire a buffer for this tensor output from the corresponding buffer pool
        HailoMediaLibraryBufferPtr tensor_buffer = std::make_shared<hailo_media_library_buffer>();
        BufferPtr tensor_buffer_ptr = std::make_shared<Buffer>(tensor_buffer);
        if (m_tensor_buffer_pools[output.name()]->acquire_buffer(tensor_buffer) != MEDIA_LIBRARY_SUCCESS)
        {
            m_debug_counters->increment_failed_acquire_buffer();
            if (m_pool_mode == StagePoolMode::FAIL_ON_EMPTY_POOL)
            {
                return AppStatus::BUFFER_ALLOCATION_ERROR;
            }
            else if (m_pool_mode == StagePoolMode::BLOCKING)
            {
                REFERENCE_CAMERA_LOG_INFO("{} acquire buffer from buffer pool failed, wait for available buffer",
                                          m_stage_name);
                std::unique_lock<std::mutex> lock(m_buff_pool_mutex);
                m_available_buffers_cv.wait(lock, [this, output, tensor_buffer] {
                    return m_tensor_buffer_pools[output.name()]->acquire_buffer(tensor_buffer) == MEDIA_LIBRARY_SUCCESS;
                });
            }
            else
            {
                for (auto &buffer : tensor_buffers)
                {
                    buffer.second.reset();
                    m_debug_counters->increment_dropped_frames();
                }
                return AppStatus::SUCCESS;
            }
        }

        // Set entry in map
        tensor_buffers[output.name()] = tensor_buffer_ptr;

        // Set the HailoRT bindings for the acquired buffer
        size_t tensor_size = output.get_frame_size();
        auto status = m_bindings.output(output.name())
                          ->set_buffer(hailort::MemoryView(tensor_buffer->get_plane_ptr(0), tensor_size));
        if (HAILO_SUCCESS != status)
        {
            std::cerr << m_stage_name << " failed to set infer output buffer " << output.name()
                      << ", Hailort status = " << status << std::endl;
            REFERENCE_CAMERA_LOG_ERROR("{} failed to set infer output buffer {} , Hailort status = ", m_stage_name,
                                       output.name(), status);
            return AppStatus::HAILORT_ERROR;
        }
    }
    return AppStatus::SUCCESS;
}

AppStatus HailortAsyncStage::infer(BufferPtr input_buffer,
                                   const std::unordered_map<std::string, BufferPtr> &tensor_buffers)
{
    // wait for infer model to be ready
    auto status = m_configured_infer_model.wait_for_async_ready(std::chrono::milliseconds(1000));
    if (HAILO_SUCCESS != status)
    {
        std::cerr << "Failed to wait for async ready, Hailort status = " << status << std::endl;
        REFERENCE_CAMERA_LOG_ERROR("Failed to wait for async ready, Hailort status = {}", status);
        return AppStatus::HAILORT_ERROR;
    }

    // Run the async infer api, when inference is done it will call the given callback
    std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
    auto job = m_configured_infer_model.run_async(
        m_bindings,
        [tensor_buffers, input_buffer, begin, this](const hailort::AsyncInferCompletionInfo &completion_info) {
            // active job finished
            --this->m_active_jobs;
            m_active_jobs_cv.notify_one();
            inference_tracing_end(input_buffer);

            // check infer status
            if (completion_info.status != HAILO_SUCCESS)
            {
                std::cerr << "Failed to run async infer, Hailort status = " << completion_info.status << std::endl;
                REFERENCE_CAMERA_LOG_ERROR("Failed to run async infer, Hailort status = {}", completion_info.status);
                return AppStatus::HAILORT_ERROR;
            }

            if (m_end_of_stream)
                return AppStatus::SUCCESS;

            // Add metadata for each output tensor buffer
            for (auto &output : m_infer_model->outputs())
            {
                m_debug_counters->increment_extra_counter(static_cast<int>(AIExtraCounters::TENSORS));
                BufferPtr tensor_buffer = tensor_buffers.at(output.name());
                TensorMetadataPtr tensor_metadata = std::make_shared<TensorMetadata>(tensor_buffer, output.name());
                input_buffer->add_metadata(tensor_metadata);

                // Add the vstream info and data pointer to the HailoRoi for later use (postprocessing)
                input_buffer->get_roi()->add_tensor(std::make_shared<HailoTensor>(
                    reinterpret_cast<uint8_t *>(tensor_buffer->get_buffer()->get_plane_ptr(0)),
                    m_vstream_infos[output.name()]));
            }

            std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
            if (m_print_fps)
            {
                REFERENCE_CAMERA_LOG_DEBUG("Inference time ({}) = {}[microseconds]", m_stage_name,
                                           std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count());
            }

            // Send the input buffer to the next stage
            input_buffer->add_time_stamp(m_stage_name);
            set_duration(input_buffer);
            m_debug_counters->increment_output_frames();
            send_to_subscribers(input_buffer);

            return AppStatus::SUCCESS;
        });
    ++m_active_jobs;

    if (!job)
    {
        std::cerr << "Failed to start async infer job, status = " << job.status() << std::endl;
        REFERENCE_CAMERA_LOG_ERROR("Failed to start async infer job, status = {}", job.status());
        return AppStatus::HAILORT_ERROR;
    }

    // detach the job to run in it's own thread on the side
    job->detach();
    m_last_infer_job = std::make_shared<hailort::AsyncInferJob>(job.release());

    return AppStatus::SUCCESS;
}

AppStatus HailortAsyncStage::process(BufferPtr data)
{
    REFERENCE_CAMERA_LOG_DEBUG("[{}] process() called", m_stage_name);
    inference_tracing_begin(data);

    m_debug_counters->increment_input_frames();
    // Wait and set scheduler threshold if dynamic thresholding used
    if (m_dynamic_threshold)
    {
        std::vector<MetadataPtr> metadata = data->get_metadata_of_type(MetadataType::BATCH);
        if (metadata.size() > 0)
        {
            BatchMetadataPtr batch_metadata = std::dynamic_pointer_cast<BatchMetadata>(metadata[0]);

            // if this is the start of a new batch, wait for the last infer job to finish
            if (batch_metadata->get_index() == 0)
            {
                REFERENCE_CAMERA_LOG_DEBUG("[{}] Waiting for active jobs to finish before new batch", m_stage_name);
                std::unique_lock<std::mutex> lock(m_active_jobs_mutex);
                m_active_jobs_cv.wait(lock, [this] { return m_active_jobs == 0 || m_end_of_stream; });

                if (m_end_of_stream)
                {
                    REFERENCE_CAMERA_LOG_INFO("[{}] End of stream detected during batch start", m_stage_name);
                    inference_tracing_end(data);

                    return AppStatus::SUCCESS;
                }
                // Dynamic scheduling threshold - set the scheduler threshold to the current size of batch
                // (described by the batch metadata)
                if (batch_metadata->get_total_size() <= (uint)m_batch_size)
                    m_configured_infer_model.set_scheduler_threshold(batch_metadata->get_total_size());
                else
                    m_configured_infer_model.set_scheduler_threshold(m_batch_size);
            }
        }
    }

    // wait for available jobs
    REFERENCE_CAMERA_LOG_DEBUG("[{}] Waiting for available job slot (active_jobs: {}, jobs_limit: {})", m_stage_name,
                               m_active_jobs.load(), m_jobs_limit);
    std::unique_lock<std::mutex> lock(m_active_jobs_mutex);
    m_active_jobs_cv.wait(lock, [this] { return m_active_jobs < m_jobs_limit; });

    // Set the input buffer
    REFERENCE_CAMERA_LOG_DEBUG("[{}] Setting pixel buffer for inference", m_stage_name);
    if (set_pix_buf(data->get_buffer()) != AppStatus::SUCCESS)
    {
        REFERENCE_CAMERA_LOG_ERROR("[{}] Failed to set pixel buffer", m_stage_name);
        inference_tracing_end(data);

        return AppStatus::HAILORT_ERROR;
    }

    // Acquire and set tensor buffers
    REFERENCE_CAMERA_LOG_DEBUG("[{}] Acquiring and setting tensor buffers", m_stage_name);
    std::unordered_map<std::string, BufferPtr> tensor_buffers;
    if (acquire_and_set_tensor_buffers(tensor_buffers) != AppStatus::SUCCESS)
    {
        REFERENCE_CAMERA_LOG_ERROR("[{}] Failed to acquire/set tensor buffers", m_stage_name);
        inference_tracing_end(data);

        return AppStatus::HAILORT_ERROR;
    }

    // Run the inference
    REFERENCE_CAMERA_LOG_DEBUG("[{}] Running inference", m_stage_name);
    if (infer(data, tensor_buffers) != AppStatus::SUCCESS)
    {
        REFERENCE_CAMERA_LOG_ERROR("[{}] Inference failed", m_stage_name);
        inference_tracing_end(data);

        return AppStatus::HAILORT_ERROR;
    }

    REFERENCE_CAMERA_LOG_DEBUG("[{}] process() completed successfully", m_stage_name);
    return AppStatus::SUCCESS;
}

std::string HailortAsyncStage::generate_unique_timestamp_str(BufferPtr data)
{
    // NOTE: We modified Perfetto. Originally, it ignored all numbers in event names, which made it impossible
    // to distinguish between events with the same name. We now use `isp_timestamp_ns` to create a unique string
    // The trick is that if the number is wrapped in curly braces, Perfetto will not ignore it.
    return m_stage_name + "{" + std::to_string(data->get_buffer()->isp_timestamp_ns) + "}";
}

uint64_t HailortAsyncStage::get_unique_buffer_identifier(BufferPtr data)
{
    uint64_t isp_timestamp_ns = data->get_buffer()->isp_timestamp_ns;
    size_t batch_index = 0;

    std::vector<MetadataPtr> metadata = data->get_metadata_of_type(MetadataType::BATCH);
    if (metadata.size() > 0)
    {
        BatchMetadataPtr batch_metadata = std::dynamic_pointer_cast<BatchMetadata>(metadata[0]);
        if (batch_metadata != nullptr)
        {
            batch_index = batch_metadata->get_index();
        }
    }

    uint64_t unique_id = isp_timestamp_ns + batch_index;
    return unique_id;
}

void HailortAsyncStage::inference_tracing_begin(BufferPtr data)
{
    std::string timestamp_str = generate_unique_timestamp_str(data);
    uint64_t unique_id = get_unique_buffer_identifier(data);
    m_tracing->trace_async_event_begin(unique_id, timestamp_str.c_str());
}

void HailortAsyncStage::inference_tracing_end(BufferPtr data)
{
    uint64_t unique_id = get_unique_buffer_identifier(data);
    m_tracing->trace_async_event_end(unique_id);
}

// Builder implementation
HailortAsyncStageBuild::Builder &HailortAsyncStageBuild::Builder::set_stage_name(std::string name)
{
    m_stage_name = name;
    return *this;
}

HailortAsyncStageBuild::Builder &HailortAsyncStageBuild::Builder::set_hef_path(std::string path)
{
    m_hef_path = path;
    return *this;
}

HailortAsyncStageBuild::Builder &HailortAsyncStageBuild::Builder::set_queue_size(size_t size)
{
    m_queue_size = size;
    return *this;
}

HailortAsyncStageBuild::Builder &HailortAsyncStageBuild::Builder::set_output_pool_size(int size)
{
    m_output_pool_size = size;
    return *this;
}

HailortAsyncStageBuild::Builder &HailortAsyncStageBuild::Builder::set_group_id(std::string id)
{
    m_group_id = id;
    return *this;
}

HailortAsyncStageBuild::Builder &HailortAsyncStageBuild::Builder::set_batch_size(int size)
{
    m_batch_size = size;
    return *this;
}

HailortAsyncStageBuild::Builder &HailortAsyncStageBuild::Builder::set_job_limit(size_t size)
{
    m_job_limit = size;
    return *this;
}

HailortAsyncStageBuild::Builder &HailortAsyncStageBuild::Builder::set_scheduler_threshold_opt(int threshold)
{
    m_scheduler_threshold = threshold;
    return *this;
}

HailortAsyncStageBuild::Builder &HailortAsyncStageBuild::Builder::set_dynamic_threshold_opt(bool activate)
{
    m_dynamic_threshold = activate;
    return *this;
}

HailortAsyncStageBuild::Builder &HailortAsyncStageBuild::Builder::set_scheduler_timeout_opt(
    std::chrono::milliseconds timeout)
{
    m_scheduler_timeout = timeout;
    return *this;
}

HailortAsyncStageBuild::Builder &HailortAsyncStageBuild::Builder::set_printfps_opt(bool activate)
{
    m_print_fps = activate;
    return *this;
}

HailortAsyncStageBuild::Builder &HailortAsyncStageBuild::Builder::set_pool_mode_opt(StagePoolMode mode)
{
    m_pool_mode = mode;
    return *this;
}

HailortAsyncStageBuild::Builder &HailortAsyncStageBuild::Builder::set_nms_score_threshold(float32_t score_threshold)
{
    m_nms_score_threshold = score_threshold;
    return *this;
}

std::shared_ptr<HailortAsyncStage> HailortAsyncStageBuild::Builder::buildptr() const
{
    THROW_IF_MISSING(m_stage_name.has_value(), "set_stage_name");
    THROW_IF_MISSING(m_hef_path.has_value(), "set_hef_path");
    THROW_IF_MISSING((m_output_pool_size > 0), "set_output_pool_size");
    THROW_IF_MISSING(m_group_id.has_value(), "set_group_id");
    THROW_IF_MISSING((m_batch_size >= 1), "set_batch_size");
    THROW_IF_MISSING((m_job_limit != 0), "set_job_limit");

    return std::make_shared<HailortAsyncStage>(m_stage_name.value(), m_hef_path.value(), m_queue_size,
                                               m_output_pool_size, m_group_id.value(), m_batch_size, m_job_limit,
                                               m_scheduler_threshold, m_dynamic_threshold, m_scheduler_timeout,
                                               m_print_fps, m_pool_mode, m_nms_score_threshold);
}

HailortAsyncStageBuild::Builder HailortAsyncStageBuild::create()
{
    return Builder();
}
