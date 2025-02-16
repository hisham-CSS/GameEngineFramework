#ifndef ENTITY_H
#define ENTITY_H

#include <entt/entt.hpp>
#include "Core.h"

namespace MyCoreEngine {

	class ENGINE_API Entity {
	public:
		Entity() = default;

		Entity(entt::entity handle, entt::registry* registry)
			: m_entityHandle(handle), m_registry(registry) {
		}

		// Add a component to the entity.
		template<typename T, typename... Args>
		T& addComponent(Args&&... args) {
			return m_registry->emplace<T>(m_entityHandle, std::forward<Args>(args)...);
		}

		// Get a component from the entity.
		template<typename T>
		T& getComponent() {
			return m_registry->get<T>(m_entityHandle);
		}

		// Check if the entity has a specific component.
		template<typename T>
		bool hasComponent() const {
			return m_registry->any_of<T>(m_entityHandle);
		}

		// Allow implicit conversion to entt::entity if needed.
		operator entt::entity() const { return m_entityHandle; }

	private:
		entt::entity m_entityHandle{ entt::null };
		entt::registry* m_registry = nullptr;
	};

} // namespace MyCoreEngine

#endif