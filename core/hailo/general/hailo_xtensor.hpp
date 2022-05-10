/**
 * Copyright (c) 2021-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the LGPL license (https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt)
 **/
#pragma once

#include "hailo_objects.hpp"
#include "xtensor/xarray.hpp"
#include "xtensor/xview.hpp"
#include <string>

namespace hailo_common
{

    inline void add_landmarks_to_detection(NewHailoDetection &detection,
                                           std::string landmarks_type,
                                           xt::xarray<float> landmarks,
                                           float threshold = 1.0f,
                                           const std::vector<std::pair<int, int>> pairs = {})
    {
        HailoBBox bbox = detection.get_bbox();
        std::vector<HailoPoint> points;
        bool has_confidence;
        /**
         * @brief Determening whether the shape of the landmarks is valid:
         *  When there are 2 columns: each point has x and y positions.
         *  When there are 3 columns: Each point has x and y positions, and a confidence.
         *  Every other number of columns is invalid.
         */
        if (landmarks.shape(1) == 2)
        {
            has_confidence = false;
        }
        else if (landmarks.shape(1) == 3)
        {
            has_confidence = true;
        }
        else
        {
            throw std::invalid_argument("Landmarks should be with 2 or 3 columns only");
        }
        // Make these points relative to the BBox they are related too.
        xt::view(landmarks, xt::all(), 0) = (xt::view(landmarks, xt::all(), 0) - bbox.xmin()) / bbox.width();
        xt::view(landmarks, xt::all(), 1) = (xt::view(landmarks, xt::all(), 1) - bbox.ymin()) / bbox.height();
        // Create HailoPoint for each point and add it to the vector of points.
        points.reserve(landmarks.shape(0));
        for (uint i = 0; i < landmarks.shape(0); i++)
        {
            if (has_confidence)
            {
                points.emplace_back(landmarks(i, 0), landmarks(i, 1), landmarks(i, 2));
            }
            else
            {
                points.emplace_back(landmarks(i, 0), landmarks(i, 1));
            }
        }
        // Add HailoLandmarks pointer to the detection.
        detection.add_object(std::make_shared<HailoLandmarks>(landmarks_type, points, threshold, pairs));
    }
}
