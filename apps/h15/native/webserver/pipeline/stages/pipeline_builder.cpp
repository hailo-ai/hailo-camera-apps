#include "pipeline_builder.hpp"
#include <stdexcept>
#include <spdlog/spdlog.h>

bool PipelineBuilder::isValidTransition(building_state from, building_state to) {
    switch (from) {
        case building_state::ADDING_STAGES:
            return to == building_state::CONNECTING;
        case building_state::CONNECTING:
            return to == building_state::BUILDING;
        case building_state::BUILDING:
            return to == building_state::DONE;
        default:
            return false;
    }
}

void PipelineBuilder::next_state(building_state state) {
    if (!isValidTransition(m_state, state)) {
        REFERENCE_CAMERA_LOG_ERROR("Invalid state transition from {} to {}",
                                    static_cast<int>(m_state), static_cast<int>(state));
        throw std::invalid_argument("Invalid state transition");
    }
    REFERENCE_CAMERA_LOG_DEBUG("Transitioning state from {} to {}",
                               static_cast<int>(m_state), static_cast<int>(state));
    m_state = state;
}

PipelineBuilder& PipelineBuilder::connect(const std::string &sourceName, const std::string &targetName) {
    if (m_state == building_state::ADDING_STAGES) {
        next_state(building_state::CONNECTING);
    }
    else if (m_state != building_state::CONNECTING) {
        REFERENCE_CAMERA_LOG_ERROR("Invalid state in connect. Expected CONNECTING but current state is {}", static_cast<int>(m_state));
        throw std::invalid_argument("PipelineBuilder is not in CONNECTING state");
    }
    if (m_frontendStages.find(sourceName) != m_frontendStages.end()) {
        REFERENCE_CAMERA_LOG_ERROR("Generic connect cannot use a FrontendStage as source: {}", sourceName);
        throw std::invalid_argument("Generic connect cannot use a FrontendStage as source: " + sourceName);
    }
    if (m_frontendStages.find(targetName) != m_frontendStages.end()) {
        REFERENCE_CAMERA_LOG_ERROR("Generic connect cannot use a FrontendStage as target: {}", targetName);
        throw std::invalid_argument("Generic connect cannot use a FrontendStage as target: " + targetName);
    }
    if (m_genericStages.find(sourceName) == m_genericStages.end()) {
        REFERENCE_CAMERA_LOG_ERROR("Source stage not found in generic stages: {}", sourceName);
        throw std::invalid_argument("Source stage not found in generic stages: " + sourceName);
    }
    if (m_genericStages.find(targetName) == m_genericStages.end()) {
        REFERENCE_CAMERA_LOG_ERROR("Target stage not found in generic stages: {}", targetName);
        throw std::invalid_argument("Target stage not found in generic stages: " + targetName);
    }
    m_connections.emplace_back(sourceName, targetName);
    REFERENCE_CAMERA_LOG_INFO("Established generic connection: {} -> {}", sourceName, targetName);
    return *this;
}

PipelineBuilder& PipelineBuilder::connectFrontend(const std::string &frontendName, const std::string &streamId, const std::string &targetName) {
    if (m_state == building_state::ADDING_STAGES) {
        next_state(building_state::CONNECTING);
    }
    else if (m_state != building_state::CONNECTING) {
        REFERENCE_CAMERA_LOG_ERROR("Invalid state in connectFrontend. Expected CONNECTING but current state is {}", static_cast<int>(m_state));
        throw std::invalid_argument("PipelineBuilder is not in CONNECTING state");
    }
    if (m_frontendStages.find(frontendName) == m_frontendStages.end()) {
        REFERENCE_CAMERA_LOG_ERROR("Source stage is not a FrontendStage: {}", frontendName);
        throw std::invalid_argument("Source stage is not a FrontendStage: " + frontendName);
    }
    if (m_frontendStages.find(targetName) != m_frontendStages.end()) {
        REFERENCE_CAMERA_LOG_ERROR("connectFrontend target cannot be a FrontendStage: {}", targetName);
        throw std::invalid_argument("connectFrontend target cannot be a FrontendStage: " + targetName);
    }
    if (m_genericStages.find(targetName) == m_genericStages.end()) {
        REFERENCE_CAMERA_LOG_ERROR("Target stage not found in generic stages: {}", targetName);
        throw std::invalid_argument("Target stage not found in generic stages: " + targetName);
    }
    m_frontendSubscriptions.emplace_back(frontendName, streamId, targetName);
    REFERENCE_CAMERA_LOG_INFO("Established frontend subscription: {} (stream: {}) -> {}", frontendName, streamId, targetName);
    return *this;
}

void PipelineBuilder::validateStagesUsage() {
    for (const auto &pair : m_allStages) {
        const std::string &stageName = pair.first;
        bool used = false;
        
        // Check if the stage is used in generic connections.
        for (const auto &conn : m_connections) {
            if (conn.first == stageName || conn.second == stageName) {
                used = true;
                break;
            }
        }
        
        // If not found in generic connections, check frontend subscriptions.
        if (!used) {
            for (const auto &sub : m_frontendSubscriptions) {
                if (std::get<0>(sub) == stageName || std::get<2>(sub) == stageName) {
                    used = true;
                    break;
                }
            }
        }
        
        if (!used) {
            REFERENCE_CAMERA_LOG_WARN("Stage {} was added but is not used in any connection or subscription.", stageName);
            throw std::invalid_argument("Stage " + stageName + " was added but not used in any connection or subscription.");
        }
        REFERENCE_CAMERA_LOG_DEBUG("Stage {} usage validated.", stageName);
    }
}

std::shared_ptr<Pipeline> PipelineBuilder::build() {
    if (m_state == building_state::CONNECTING) {
        next_state(building_state::BUILDING);
    }
    else if (m_state != building_state::BUILDING) {
        REFERENCE_CAMERA_LOG_ERROR("Invalid state in build. Expected CONNECTING or BUILDING, but current state is {}", static_cast<int>(m_state));
        throw std::invalid_argument("PipelineBuilder is not in CONNECTING state");
    }
    REFERENCE_CAMERA_LOG_INFO("Building pipeline with {} stages", m_allStages.size());
    auto pipeline = std::make_shared<Pipeline>();
    
    // Add all stages to the pipeline.
    for (auto &pair : m_allStages) {
        const std::string &name = pair.first;
        auto stage = pair.second;
        auto it = m_stageTypes.find(name);
        if (it != m_stageTypes.end()) {
            pipeline->add_stage(stage, it->second);
            REFERENCE_CAMERA_LOG_DEBUG("Added stage {} with specific type", name);
        }
        else {
            pipeline->add_stage(pair.second);
            REFERENCE_CAMERA_LOG_DEBUG("Added stage {} with default type", name);
        }
    }
    
    // Establish generic connections.
    for (const auto &conn : m_connections) {
        const std::string &srcName = conn.first;
        const std::string &tgtName = conn.second;
        auto source = m_genericStages.at(srcName);
        auto target = m_genericStages.at(tgtName);
        source->add_subscriber(target);
        REFERENCE_CAMERA_LOG_INFO("Established generic connection: {} -> {}", srcName, tgtName);
    }
    
    // Establish frontend subscriptions.
    for (const auto &entry : m_frontendSubscriptions) {
        const std::string &frontendName = std::get<0>(entry);
        const std::string &streamId = std::get<1>(entry);
        const std::string &targetName = std::get<2>(entry);
        
        auto frontendStage = m_frontendStages.at(frontendName);
        auto targetStage = m_genericStages.at(targetName);
        frontendStage->subscribe_to_stream(streamId, targetStage);
        REFERENCE_CAMERA_LOG_INFO("Established frontend subscription: {} (stream: {}) -> {}", frontendName, streamId, targetName);
    }
    
    // Validate that every stage has been used.
    try {
        validateStagesUsage();
    } catch (const std::exception &ex) {
        REFERENCE_CAMERA_LOG_ERROR("Stage validation failed: {}", ex.what());
        throw;
    }
    
    next_state(building_state::DONE);
    REFERENCE_CAMERA_LOG_INFO("Pipeline build completed. Transitioned to DONE state.");
    return pipeline;
}
