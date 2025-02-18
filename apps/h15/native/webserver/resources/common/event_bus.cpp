#include "event_bus.hpp"

void EventBus::subscribe(EventType event_type, EventPriority priority, ResourceChangeCallback callback) {
    m_callbacks[event_type][priority].emplace_back(std::move(callback));
}

void EventBus::notify(EventType event_type, std::shared_ptr<ResourceState> data) {
    // Find the event type in the map
    auto it = m_callbacks.find(event_type);
    if (it != m_callbacks.end()) {
        // Call the callbacks in order of priority
        std::map<EventPriority, std::vector<ResourceChangeCallback>>& priority_callbacks = it->second;
        for (auto& [priority, callbacks] : priority_callbacks) {
            WEBSERVER_LOG_DEBUG("Calling callbacks for event type {} with priority {}", static_cast<nlohmann::json>(event_type).dump(), priority);
            for (auto& callback : callbacks) {
                callback({event_type, data});
            }
        }
    }
}