#pragma once

#include <functional>
#include <unordered_map>
#include <vector>
#include <memory>
#include <string>
#include "common/logger_macros.hpp"
#include "events_utils.hpp"
using namespace webserver::resources;
class EventBus {
public:

    /**
     * Subscribe to a specific event with a callback.
     * @param event_name The name of the event to subscribe to.
     * @param callback The function to be executed when the event is triggered.
     */
    void subscribe(EventType event_type, EventPriority prority, ResourceChangeCallback callback);

    /**
     * Notify all subscribers of a specific event.
     * @param event_name The name of the event to trigger.
     * @param data The data to pass to subscribers.
     */
    void notify(EventType event_type, std::shared_ptr<ResourceState> data);

private:
    std::unordered_map<EventType, std::map<EventPriority, std::vector<ResourceChangeCallback>>> m_callbacks;
};
