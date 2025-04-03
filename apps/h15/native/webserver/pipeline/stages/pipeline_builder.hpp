#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <stdexcept>
#include <tuple>
#include <type_traits>

#include "pipeline.hpp"
#include "encoder_stage.hpp"
#include "frontend_stage.hpp"

class PipelineBuilder {
public:
    template <typename T>
    PipelineBuilder& addStage(const std::string &name, std::shared_ptr<T> stage, StageType type = StageType::GENERAL) {
        if (m_state != building_state::ADDING_STAGES){
            throw std::invalid_argument("PipelineBuilder is not in ADDING_STAGES state");
        }
        static_assert(std::is_base_of_v<ConnectedStage, T>, "T must derive from Stage");
        if (!stage) {
            throw std::invalid_argument("Stage is null for name: " + name);
        }

        if (m_allStages.find(name) != m_allStages.end()) {
            throw std::invalid_argument("Stage already exists: " + name);
        }

        if (type != StageType::GENERAL) {
            m_stageTypes[name] = type;
        }

        if constexpr (std::is_base_of_v<FrontendStage, T>) {
            m_frontendStages[name] = stage;
        } else {
            m_genericStages[name] = stage;
        }

        m_allStages[name] = stage;
        return *this;
    }
    
    // Connect a generic (non-frontend) stage's output to another non-frontend stage's input.
    // Uses add_subscriber.
    PipelineBuilder& connect(const std::string &sourceName, const std::string &targetName);
    
    // Connect a FrontendStage's output (for a given stream) to a target stage.
    // The stream identifier is now a string.
    PipelineBuilder& connectFrontend(const std::string &frontendName, const std::string &streamId, const std::string &targetName);
    

    // Build the pipeline with the registered stages and connections.
    std::shared_ptr<Pipeline> build();



private:
    std::unordered_map<std::string, std::shared_ptr<ConnectedStage>> m_allStages;
    std::unordered_map<std::string, std::shared_ptr<ConnectedStage>> m_genericStages;
    std::unordered_map<std::string, std::shared_ptr<FrontendStage>> m_frontendStages;
    std::vector<std::pair<std::string, std::string>> m_connections;
    std::vector<std::tuple<std::string, std::string, std::string>> m_frontendSubscriptions;
    std::unordered_map<std::string, StageType> m_stageTypes;

    enum class building_state{
        ADDING_STAGES,
        CONNECTING,
        BUILDING,
        DONE
    };
    building_state m_state = building_state::ADDING_STAGES;

    //sransition to next state
    void next_state(building_state state);

    bool isValidTransition(building_state from, building_state to);

    // Validate that all stages are used in connections or subscriptions.
    void validateStagesUsage();
};