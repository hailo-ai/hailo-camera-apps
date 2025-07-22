#include "event_bus.hpp"

void EventBus::subscribe(EventType event_type, EventPriority priority, const ResourceChangeCallback &callback)
{
    WEBSERVER_LOG_INFO("Subscribing to event type {} with priority {}", nlohmann::json(event_type).dump(),
    nlohmann::json(priority).dump());
    m_callbacks[event_type][priority].emplace_back(callback);
}

void EventBus::subscribe(std::initializer_list<EventType> event_types, EventPriority priority,
                         const ResourceChangeCallback &callback)
{
    for (auto event_type : event_types)
    {
        subscribe(event_type, priority, callback);
    }
}
void EventBus::subscribe_async(EventType event_type, EventPriority priority, const ResourceChangeCallback &callback)
{
    auto async_callback = [callback](auto... args) {
        std::thread([callback, args...]() { callback(args...); }).detach();
    };
    WEBSERVER_LOG_INFO("Subscribing to event type {} with priority {}", nlohmann::json(event_type).dump(),
    nlohmann::json(priority).dump());
    m_callbacks[event_type][priority].emplace_back(async_callback);
}

void EventBus::subscribe_async(std::initializer_list<EventType> event_types, EventPriority priority,
                               const ResourceChangeCallback &callback)
{
    for (auto event_type : event_types)
    {
        subscribe_async(event_type, priority, callback);
    }
}
