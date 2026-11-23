#pragma once

#include <memory>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace componeng::events {

class IEventQueue {
public:
  virtual ~IEventQueue() = default;
  virtual void swapBuffers() = 0;
};

template <typename EventType> class EventQueue : public IEventQueue {
public:
  void emit(const EventType &event) {
    m_write.push_back(event);
  }

  const std::vector<EventType> &getEvents() const {
    return m_read;
  }

  void swapBuffers() override {
    m_read.swap(m_write);
    m_write.clear();
  }

private:
  std::vector<EventType> m_read;
  std::vector<EventType> m_write;
};

class EventBus {
public:
  template <typename EventType> void emit(const EventType &event) {
    getQueue<EventType>().emit(event);
  }

  template <typename EventType> const std::vector<EventType> &getEvents() {
    return getQueue<EventType>().getEvents();
  }

  void swapBuffers() {
    for (auto &pair : m_queues) {
      pair.second->swapBuffers();
    }
  }

private:
  std::unordered_map<std::type_index, std::unique_ptr<IEventQueue>> m_queues;

  template <typename EventType> EventQueue<EventType> &getQueue() {
    std::type_index typeIndex(typeid(EventType));
    auto it = m_queues.find(typeIndex);
    if (it == m_queues.end()) {
      auto queue = std::make_unique<EventQueue<EventType>>();
      it = m_queues.emplace(typeIndex, std::move(queue)).first;
    }
    return *static_cast<EventQueue<EventType> *>(it->second.get());
  }
};

} // namespace componeng::events
