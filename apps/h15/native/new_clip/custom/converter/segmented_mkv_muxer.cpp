#include "custom/converter/segmented_mkv_muxer.hpp"

GStreamerMkvSegmenter::GStreamerMkvSegmenter(CodecType codec, const std::string &output_path,
                                             const std::string &file_prefix, uint32_t segment_duration_sec)
    : m_codec_type(codec), m_output_path(output_path), m_file_prefix(file_prefix),
      m_segment_duration_sec(segment_duration_sec), m_epoch_naming_data(nullptr), m_notification_callback(nullptr),
      m_callback_user_data(nullptr), m_pipeline(nullptr), m_appsrc(nullptr), m_parser(nullptr), m_muxer(nullptr),
      m_bus(nullptr), m_last_segment_end_running_time(0), m_current_segment_start_running_time(0),
      m_processing_active(false), m_initialized(false), m_running(false)
{

    m_epoch_naming_data = new EpochNamingData(output_path, file_prefix);
}

GStreamerMkvSegmenter::~GStreamerMkvSegmenter()
{

    // 1. FIRST: Stop pipeline to prevent new segments
    if (m_running)
    {
        stop();
    }

    // 2. SECOND: Invalidate naming data before cleanup
    if (m_epoch_naming_data)
    {
        std::lock_guard<std::mutex> lock(m_epoch_naming_data->data_mutex);
        m_epoch_naming_data->is_valid = false;
    }

    // 3. THIRD: Cleanup GStreamer resources
    cleanup();

    // Delete naming data after cleanup
    delete m_epoch_naming_data;
    m_epoch_naming_data = nullptr;
}

bool GStreamerMkvSegmenter::initialize()
{
    if (m_initialized)
    {
        return true;
    }

    // Initialize GStreamer
    if (!gst_is_initialized())
    {
        gst_init(nullptr, nullptr);
    }

    if (!create_pipeline())
    {
        g_printerr("Failed to create GStreamer pipeline\n");
        return false;
    }

    m_initialized = true;
    return true;
}

bool GStreamerMkvSegmenter::create_pipeline()
{
    // Create pipeline elements
    m_pipeline = gst_pipeline_new("mkv-segmenter");
    if (!m_pipeline)
    {
        g_printerr("Failed to create pipeline\n");
        return false;
    }

    // Create appsrc
    m_appsrc = gst_element_factory_make("appsrc", "source");
    if (!m_appsrc)
    {
        g_printerr("Failed to create appsrc\n");
        return false;
    }

    // Create parser based on codec type
    const char *parser_name = (m_codec_type == CodecType::H264) ? "h264parse" : "h265parse";
    m_parser = gst_element_factory_make(parser_name, "parser");
    if (!m_parser)
    {
        g_printerr("Failed to create %s\n", parser_name);
        return false;
    }

    // Create splitmuxsink instead of muxer + multifilesink
    GstElement *splitmuxsink = gst_element_factory_make("splitmuxsink", "splitsink");
    if (!splitmuxsink)
    {
        g_printerr("Failed to create splitmuxsink (requires GStreamer 1.8+)\n");
        return false;
    }

    // Configure appsrc
    GstCaps *caps;
    if (m_codec_type == CodecType::H264)
    {
        caps = gst_caps_new_simple("video/x-h264", "stream-format", G_TYPE_STRING, "byte-stream", "alignment",
                                   G_TYPE_STRING, "nal", nullptr);
    }
    else
    {
        caps = gst_caps_new_simple("video/x-h265", "stream-format", G_TYPE_STRING, "byte-stream", "alignment",
                                   G_TYPE_STRING, "nal", nullptr);
    }

    g_object_set(G_OBJECT(m_appsrc), "caps", caps, "format", GST_FORMAT_TIME, "is-live", TRUE, "do-timestamp", FALSE,
                 nullptr);
    gst_caps_unref(caps);

    // Configure parser
    g_object_set(G_OBJECT(m_parser), "config-interval", -1, nullptr);

    // Configure splitmuxsink - SAFE VERSION
    std::string location_pattern = generate_location_pattern();

    /*
    g_print("=== SPLITMUXSINK DEBUG INFO ===\n");
    g_print("Location pattern: %s\n", location_pattern.c_str());
    g_print("Segment duration: %u seconds\n", m_segment_duration_sec);
    */

    g_object_set(G_OBJECT(splitmuxsink), "max-size-time", (guint64)(m_segment_duration_sec * GST_SECOND),
                 "muxer-factory", "matroskamux",
                 // KEY: Set muxer properties to fix seeking/duration
                 "muxer-properties",
                 gst_structure_new("properties", "streamable", G_TYPE_BOOLEAN, FALSE, "offset-to-zero", G_TYPE_BOOLEAN,
                                   TRUE, // Reset timestamps to 0
                                   "writing-app", G_TYPE_STRING, "GStreamerMkvSegmenter", nullptr),
                 "async-finalize", TRUE, // Ensure proper file finalization
                 nullptr);

    // Connect callback with safe data (NOT 'this')
    g_signal_connect(splitmuxsink, "format-location-full", G_CALLBACK(on_epoch_format_location_safe),
                     m_epoch_naming_data);

    // Add elements to pipeline
    gst_bin_add_many(GST_BIN(m_pipeline), m_appsrc, m_parser, splitmuxsink, nullptr);

    // Link elements
    if (!gst_element_link_many(m_appsrc, m_parser, splitmuxsink, nullptr))
    {
        g_printerr("Failed to link elements\n");
        return false;
    }

    // Set up bus
    m_bus = gst_element_get_bus(m_pipeline);
    gst_bus_set_sync_handler(m_bus, on_bus_message, this, nullptr);

    // Set up callbacks for appsrc
    g_signal_connect(m_appsrc, "need-data", G_CALLBACK(on_need_data), this);
    g_signal_connect(m_appsrc, "enough-data", G_CALLBACK(on_enough_data), this);

    return true;
}

bool GStreamerMkvSegmenter::start()
{
    if (!m_initialized)
    {
        g_printerr("Segmenter not initialized\n");
        return false;
    }

    if (m_running)
    {
        return true;
    }

    // Start processing thread
    m_processing_active = true;
    m_processing_thread = std::thread(&GStreamerMkvSegmenter::process_frame_queue, this);

    // Start pipeline
    GstStateChangeReturn ret = gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE)
    {
        g_printerr("Failed to start pipeline\n");
        m_processing_active = false;
        if (m_processing_thread.joinable())
        {
            m_processing_thread.join();
        }
        return false;
    }

    m_running = true;
    return true;
}

bool GStreamerMkvSegmenter::stop()
{
    if (!m_running)
    {
        return true;
    }

    // Stop processing thread
    m_processing_active = false;
    m_queue_cv.notify_all();
    if (m_processing_thread.joinable())
    {
        m_processing_thread.join();
    }

    // Send EOS
    gst_app_src_end_of_stream(GST_APP_SRC(m_appsrc));

    // Wait for EOS
    GstMessage *msg = gst_bus_timed_pop_filtered(m_bus, GST_CLOCK_TIME_NONE, GST_MESSAGE_EOS);
    if (msg)
    {
        gst_message_unref(msg);
    }

    // Stop pipeline
    gst_element_set_state(m_pipeline, GST_STATE_NULL);

    m_running = false;
    return true;
}

void GStreamerMkvSegmenter::cleanup()
{
    if (m_running)
    {
        stop();
    }

    destroy_pipeline();
    m_initialized = false;
}

void GStreamerMkvSegmenter::destroy_pipeline()
{
    if (m_bus)
    {
        gst_object_unref(m_bus);
        m_bus = nullptr;
    }

    if (m_pipeline)
    {
        gst_object_unref(m_pipeline);
        m_pipeline = nullptr;
    }

    m_appsrc = nullptr;
    m_parser = nullptr;
    m_muxer = nullptr;
}

void GStreamerMkvSegmenter::set_segment_notification_callback(SegmentNotificationCallback callback, void *user_data)
{
    m_notification_callback = callback;
    m_callback_user_data = user_data;
}

bool GStreamerMkvSegmenter::feed_frame(const uint8_t *nal_data, size_t size, uint64_t pts_ns)
{
    if (!m_running)
    {
        return false;
    }

    // Create frame data
    FrameData frame(nal_data, size, pts_ns);
    frame.is_keyframe = is_keyframe(nal_data, size);

    // Add to queue
    {
        std::lock_guard<std::mutex> lock(m_queue_mutex);
        m_frame_queue.push(std::move(frame));
    }
    m_queue_cv.notify_one();

    return true;
}

void GStreamerMkvSegmenter::process_frame_queue()
{
    while (m_processing_active)
    {
        std::unique_lock<std::mutex> lock(m_queue_mutex);
        m_queue_cv.wait(lock, [this] { return !m_frame_queue.empty() || !m_processing_active; });

        if (!m_processing_active)
        {
            break;
        }

        // Process frames
        std::queue<FrameData> frames_to_process;
        frames_to_process = std::move(m_frame_queue);
        m_frame_queue = std::queue<FrameData>();

        lock.unlock();

        // Process frames
        while (!frames_to_process.empty())
        {
            FrameData &frame = frames_to_process.front();

            // Create GStreamer buffer
            GstBuffer *buffer = gst_buffer_new_allocate(nullptr, frame.data.size(), nullptr);
            GstMapInfo map;
            gst_buffer_map(buffer, &map, GST_MAP_WRITE);
            memcpy(map.data, frame.data.data(), frame.data.size());
            gst_buffer_unmap(buffer, &map);

            // Set timestamps
            GST_BUFFER_PTS(buffer) = frame.pts;
            if (frame.dts != 0)
            {
                GST_BUFFER_DTS(buffer) = frame.dts;
            }

            // Push buffer
            GstFlowReturn ret = gst_app_src_push_buffer(GST_APP_SRC(m_appsrc), buffer);
            if (ret != GST_FLOW_OK)
            {
                g_printerr("Failed to push buffer: %d\n", ret);
                break;
            }

            frames_to_process.pop();
        }
    }
}

bool GStreamerMkvSegmenter::is_keyframe(const uint8_t *nal_data, size_t size) const
{
    if (size < 5)
        return false;

    // Parse all NAL units in the frame to find keyframe indicators
    const uint8_t *data = nal_data;
    size_t remaining = size;
    bool found_keyframe = false;

    while (remaining > 4)
    {
        // Find start code (0x00 0x00 0x00 0x01 or 0x00 0x00 0x01)
        size_t start_code_size = 0;

        if (remaining >= 4 && data[0] == 0 && data[1] == 0 && data[2] == 0 && data[3] == 1)
        {
            start_code_size = 4;
        }
        else if (remaining >= 3 && data[0] == 0 && data[1] == 0 && data[2] == 1)
        {
            start_code_size = 3;
        }
        else
        {
            // Move to next byte and continue searching
            data++;
            remaining--;
            continue;
        }

        // Move past start code
        data += start_code_size;
        remaining -= start_code_size;

        if (remaining == 0)
            break;

        // Parse NAL unit header
        if (m_codec_type == CodecType::H264)
        {
            uint8_t nal_header = data[0];
            uint8_t nal_type = nal_header & 0x1F;

            // H264 keyframe indicators:
            // 5 = IDR slice (primary keyframe)
            // 7 = SPS (sequence start, indicates keyframe)
            // 8 = PPS (picture parameters, often with keyframes)
            if (nal_type == 5)
            {
                found_keyframe = true;
                break; // IDR is definitive keyframe
            }
            else if (nal_type == 7)
            {
                found_keyframe = true; // Don't break, look for IDR
            }
            else if (nal_type == 8)
            {
                found_keyframe = true; // Don't break, look for IDR
            }
        }
        else
        { // H265
            if (remaining < 2)
                break;

            uint16_t nal_header = (data[0] << 8) | data[1];
            uint8_t nal_type = (nal_header >> 9) & 0x3F;

            // g_print("H265 NAL type: %u (header: 0x%04x) ", nal_type, nal_header);

            // H265 keyframe indicators:
            // 19-20 = IDR slices (primary keyframes)
            // 21 = CRA (Clean Random Access)
            // 32 = VPS, 33 = SPS, 34 = PPS (sequence/picture parameters)
            if ((nal_type >= 19 && nal_type <= 21))
            {
                found_keyframe = true;
                break; // IDR/CRA is definitive keyframe
            }
            else if (nal_type >= 32 && nal_type <= 34)
            {
                found_keyframe = true; // Don't break, look for IDR/CRA
            }
        }

        // Find next NAL unit by looking for next start code
        bool found_next = false;
        for (size_t i = 1; i < remaining; i++)
        {
            if (i + 3 < remaining && data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 0 && data[i + 3] == 1)
            {
                data += i;
                remaining -= i;
                found_next = true;
                break;
            }
            else if (i + 2 < remaining && data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1)
            {
                data += i;
                remaining -= i;
                found_next = true;
                break;
            }
        }

        if (!found_next)
        {
            // No more NAL units found
            break;
        }
    }

    return found_keyframe;
}

std::string GStreamerMkvSegmenter::generate_location_pattern() const
{
    return m_output_path + "/" + m_file_prefix + "_%06d.mkv";
}

uint64_t GStreamerMkvSegmenter::get_current_epoch_time_ms() const
{
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    return static_cast<uint64_t>(millis);
}

// Static callback implementations
void GStreamerMkvSegmenter::on_need_data(GstAppSrc *src, guint length, gpointer user_data)
{
    // This callback is triggered when the appsrc needs more data
    // We handle data pushing in our own thread, so we don't need to do anything here
}

void GStreamerMkvSegmenter::on_enough_data(GstAppSrc *src, gpointer user_data)
{
    // This callback is triggered when the appsrc has enough data
    // We can use this to implement flow control if needed
}

GstBusSyncReply GStreamerMkvSegmenter::on_bus_message(GstBus *bus, GstMessage *message, gpointer user_data)
{
    GStreamerMkvSegmenter *segmenter = static_cast<GStreamerMkvSegmenter *>(user_data);

    switch (GST_MESSAGE_TYPE(message))
    {
    case GST_MESSAGE_ERROR: {
        GError *error;
        gchar *debug;
        gst_message_parse_error(message, &error, &debug);
        g_printerr("ERROR from element %s: %s\n", GST_OBJECT_NAME(message->src), error->message);
        g_printerr("Debug info: %s\n", debug ? debug : "none");
        g_clear_error(&error);
        g_free(debug);
        break;
    }
    case GST_MESSAGE_WARNING: {
        GError *error;
        gchar *debug;
        gst_message_parse_warning(message, &error, &debug);
        g_printerr("WARNING from element %s: %s\n", GST_OBJECT_NAME(message->src), error->message);
        g_printerr("Debug info: %s\n", debug ? debug : "none");
        g_clear_error(&error);
        g_free(debug);
        break;
    }
    case GST_MESSAGE_EOS:
        break;
    case GST_MESSAGE_ELEMENT: {
        const GstStructure *s = gst_message_get_structure(message);

        // Debug: Print all element messages
        /*
        gchar* struct_str = gst_structure_to_string(s);
        g_print("ELEMENT MESSAGE: %s\n", struct_str);
        g_free(struct_str);
        */

        // Handle fragment opened (new segment starts)
        if (gst_structure_has_name(s, "splitmuxsink-fragment-opened"))
        {

            const gchar *filename = gst_structure_get_string(s, "location");
            guint64 running_time = 0;

            gst_structure_get_uint64(s, "running-time", &running_time);

            // g_print("  New file started: %s\n", filename ? filename : "NULL");
            // g_print("  Start running time: %lu ns (%.3f sec)\n", running_time, running_time / (double)GST_SECOND);

            // Track the start of this segment
            segmenter->m_current_segment_start_running_time = running_time;

            // Extract segment index and store start time
            if (filename)
            {
                uint32_t segment_index = segmenter->extract_segment_index(filename);
                segmenter->m_segment_start_times[segment_index] = running_time;
                // g_print("  Stored start time for segment %u: %lu ns\n", segment_index, running_time);
            }
        }
        // Handle fragment closed (segment complete)
        else if (gst_structure_has_name(s, "splitmuxsink-fragment-closed"))
        {

            const gchar *filename = gst_structure_get_string(s, "location");
            guint64 running_time = 0;

            gst_structure_get_uint64(s, "running-time", &running_time);

            // g_print("  File closed: %s\n", filename ? filename : "NULL");
            // g_print("  End running time: %lu ns (%.3f sec)\n", running_time, running_time / (double)GST_SECOND);

            if (filename && segmenter)
            {
                segmenter->handle_split_mux_segment_with_running_time(filename, running_time);
            }

            // Update for next segment
            segmenter->m_last_segment_end_running_time = running_time;
        }
        break;
    }
    default:
        break;
    }

    return GST_BUS_PASS;
}

gchar *GStreamerMkvSegmenter::on_epoch_format_location_safe(GstElement *splitmux, guint fragment_id,
                                                            GstSample *first_sample, gpointer user_data)
{

    // Check if user_data is valid
    if (!user_data)
    {
        g_printerr("ERROR: user_data is NULL\n");
        return g_strdup_printf("error_%u_%lu.mkv", fragment_id, (uint64_t)time(nullptr));
    }

    EpochNamingData *data = static_cast<EpochNamingData *>(user_data);

    // Thread-safe access
    std::lock_guard<std::mutex> lock(data->data_mutex);

    // Check if data is still valid
    if (!data->is_valid)
    {
        g_printerr("ERROR: naming data is no longer valid\n");
        return g_strdup_printf("invalid_%u_%lu.mkv", fragment_id, (uint64_t)time(nullptr));
    }

    // Get current epoch timestamp, we can either use up to seconds or milliseconds
    uint64_t epoch_timestamp = (uint64_t)time(nullptr);
    // uint64_t epoch_timestamp =
    // std::chrono::duration_cast<std::chrono::milliseconds>(system_clock::now().time_since_epoch()).count();

    // Generate filename using COPIED strings (safe)
    gchar *filename =
        g_strdup_printf("%s/%s_%lu.mkv", data->output_path.c_str(), data->file_prefix.c_str(), epoch_timestamp);

    // g_print("Generated safe epoch filename: %s\n", filename);
    // g_print("Fragment ID: %u, Epoch: %lu\n", fragment_id, epoch_timestamp);

    // Increment counter (thread-safe)
    data->segment_counter.fetch_add(1);

    return filename;
}

// Helper method to extract segment index from filename
uint32_t GStreamerMkvSegmenter::extract_segment_index(const char *filename)
{
    if (!filename)
        return 0;

    std::string filename_str(filename);
    size_t pos = filename_str.find_last_of("_");
    if (pos != std::string::npos)
    {
        size_t dot_pos = filename_str.find_last_of(".");
        if (dot_pos != std::string::npos && dot_pos > pos)
        {
            std::string index_str = filename_str.substr(pos + 1, dot_pos - pos - 1);
            return static_cast<uint32_t>(std::stoul(index_str));
        }
    }
    return 0;
}

void GStreamerMkvSegmenter::handle_split_mux_segment_with_running_time(const char *filename, uint64_t end_running_time)
{

    // Extract segment index
    uint32_t segment_index = extract_segment_index(filename);
    // g_print("Segment index: %u\n", segment_index);

    // Find the start running time for this segment
    uint64_t start_running_time = 0;
    auto it = m_segment_start_times.find(segment_index);
    if (it != m_segment_start_times.end())
    {
        start_running_time = it->second;
        // g_print("Found start running time: %lu ns (%.3f sec)\n", start_running_time, start_running_time /
        // (double)GST_SECOND);
    }
    else
    {
        // Fallback: use the last segment's end time
        start_running_time = m_last_segment_end_running_time;
        g_print("WARNING: Using fallback start time: %lu ns (%.3f sec)\n", start_running_time,
                start_running_time / (double)GST_SECOND);
    }

    // Calculate ACTUAL duration from running times
    uint64_t duration_ns = (end_running_time > start_running_time) ? (end_running_time - start_running_time) : 0;

    uint32_t duration_ms = static_cast<uint32_t>(duration_ns / 1000000);

    /*
    g_print("*** CALCULATED ACTUAL DURATION ***\n");
    g_print("  Start running time: %lu ns (%.3f sec)\n", start_running_time, start_running_time / (double)GST_SECOND);
    g_print("  End running time:   %lu ns (%.3f sec)\n", end_running_time, end_running_time / (double)GST_SECOND);
    g_print("  Duration:           %lu ns (%.3f sec = %u ms)\n", duration_ns, duration_ns / (double)GST_SECOND,
    duration_ms);
    */

    // Calculate start time epoch (approximate)
    uint64_t start_time_epoch_ms = get_current_epoch_time_ms() - (duration_ms);

    // Create segment info
    SegmentInfo info;
    info.filename = std::string(filename);
    info.index = segment_index;
    info.completed = true;
    info.start_time_epoch_ms = start_time_epoch_ms;

    // **CALL NOTIFICATION CALLBACK WITH ACCURATE DURATION**
    if (m_notification_callback)
    {

        /*
        g_print("*** CALLING NOTIFICATION CALLBACK OF CLOSING FILE WITH ACCURATE DURATION ***\n");
        g_print("  - Filename: %s\n", info.filename.c_str());
        g_print("  - ACCURATE Duration: %u ms\n", duration_ms);
        g_print("  - Start time: %lu ms\n", info.start_time_epoch_ms);
        g_print("  - Index: %u\n", info.index);
        */

        m_notification_callback(info.filename.c_str(),
                                duration_ms, // ACCURATE duration from running-time calculation
                                info.start_time_epoch_ms, info.index, m_callback_user_data);
    }
    else
    {
        g_print("WARNING: m_notification_callback is NULL\n");
    }

    // Clean up old start times
    if (m_segment_start_times.size() > 10)
    {
        auto oldest = m_segment_start_times.begin();
        m_segment_start_times.erase(oldest);
    }
}
