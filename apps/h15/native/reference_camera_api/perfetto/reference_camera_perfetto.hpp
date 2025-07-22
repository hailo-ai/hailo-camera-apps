/*
 * Copyright (c) 2017-2025 Hailo Technologies Ltd. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#pragma once

#ifdef HAVE_PERFETTO

#include <hailo_perfetto.h>

#define REFERENCE_CAMERA_CATEGORY "reference_camera"

PERFETTO_DEFINE_CATEGORIES(perfetto::Category(REFERENCE_CAMERA_CATEGORY)
                               .SetTags("hailo")
                               .SetDescription("Events from reference camera infrastructure"));

#define REFERENCE_CAMERA_TRACE_EVENT(...) TRACE_EVENT(REFERENCE_CAMERA_CATEGORY, ##__VA_ARGS__)
#define REFERENCE_CAMERA_TRACE_COUNTER(...) TRACE_COUNTER(REFERENCE_CAMERA_CATEGORY, ##__VA_ARGS__)

/*
We need VA_ARGS to be able to pass both with and without track. the track is used for async events.
*/
#define REFERENCE_CAMERA_TRACE_EVENT_BEGIN(event_name, ...)                                                            \
    TRACE_EVENT_BEGIN(REFERENCE_CAMERA_CATEGORY, (event_name), ##__VA_ARGS__)
#define REFERENCE_CAMERA_TRACE_EVENT_END(...) TRACE_EVENT_END(REFERENCE_CAMERA_CATEGORY __VA_OPT__(, )##__VA_ARGS__)

/* async event API - will create a dedicated track for this async event. event_name has to match between _BEGIN and _END
 */
#define REFERENCE_CAMERA_TRACE_ASYNC_EVENT_BEGIN(event_name, id)                                                       \
    REFERENCE_CAMERA_TRACE_EVENT_BEGIN((event_name), perfetto::NamedTrack(perfetto::DynamicString(event_name), (id)))
#define REFERENCE_CAMERA_TRACE_ASYNC_EVENT_END(event_name, id)                                                         \
    REFERENCE_CAMERA_TRACE_EVENT_END(perfetto::NamedTrack(perfetto::DynamicString(event_name), (id)))

#define REFERENCE_CAMERA_TRACE_ASYNC_EVENT_BEGIN_WITH_TRACK(event_name, id, track_name)                                \
    REFERENCE_CAMERA_TRACE_EVENT_BEGIN((event_name), perfetto::NamedTrack(perfetto::DynamicString(track_name), (id)))

#else // no HAVE_PERFETTO

/*
assert either HAVE_PERFETTO or PERFETTO_NOT_FOUND is defined to avoid meson bugs
*/
#ifndef PERFETTO_NOT_FOUND
#error "Perfetto define not found - probably meson target is missing common_args"
#endif // no PERFETTO_NOT_FOUND

/* no perfetto - empty macros */
#define REFERENCE_CAMERA_TRACE_EVENT(name, ...)
#define REFERENCE_CAMERA_TRACE_COUNTER(...)

#define REFERENCE_CAMERA_TRACE_EVENT_BEGIN(event_name, ...)
#define REFERENCE_CAMERA_TRACE_EVENT_END(...)
#define REFERENCE_CAMERA_TRACE_ASYNC_EVENT_BEGIN(event_name, id)
#define REFERENCE_CAMERA_TRACE_ASYNC_EVENT_END(event_name, id)
#define REFERENCE_CAMERA_TRACE_ASYNC_EVENT_BEGIN_WITH_TRACK(event_name, id, track_name)
#endif // HAVE_PERFETTO
