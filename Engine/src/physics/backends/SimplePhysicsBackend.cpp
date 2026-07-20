#include "SimplePhysicsBackend.h"

#include <algorithm>
#include <cmath>

namespace MyCoreEngine {

    bool SimplePhysicsBackend::initialize(const PhysicsSettings& settings) {
        settings_ = settings;
        bodies_.clear();
        nextId_ = 1;
        return true;
    }

    void SimplePhysicsBackend::shutdown() {
        bodies_.clear();
        nextId_ = 1;
    }

    float SimplePhysicsBackend::shapeBottomOffset(const ShapeDesc& s) {
        switch (s.type) {
        case ShapeType::Box:     return s.halfExtents.y;
        case ShapeType::Sphere:  return s.radius;
        case ShapeType::Capsule: return s.halfHeight + s.radius;
        case ShapeType::Plane:   return 0.f;
        }
        return 0.f;
    }

    BodyId SimplePhysicsBackend::createBody(const BodyDesc& desc) {
        const uint64_t id = nextId_++;
        Body b;
        b.desc = desc;
        b.state.position = desc.position;
        b.state.rotation = desc.rotation;
        b.state.linearVelocity = desc.linearVelocity;
        b.state.angularVelocity = desc.angularVelocity;
        bodies_.emplace(id, std::move(b));
        return BodyId{ id };
    }

    void SimplePhysicsBackend::destroyBody(BodyId id) {
        bodies_.erase(id.value);
    }

    void SimplePhysicsBackend::destroyAllBodies() {
        bodies_.clear();
        events_.clear();
    }

    size_t SimplePhysicsBackend::bodyCount() const {
        return bodies_.size();
    }

    void SimplePhysicsBackend::step(float fixedDt) {
        if (fixedDt <= 0.f) return;

        // Collect static support surfaces once per step. Only the two shapes
        // that can act as ground here: infinite planes and static boxes.
        events_.clear(); // events belong to THIS step only

        struct Support {
            uint64_t id;
            uint64_t userData;
            float topY;
            bool  infinite;      // plane: no horizontal bounds
            glm::vec3 min, max;  // box XZ bounds (world), when !infinite
        };
        std::vector<Support> supports;
        for (const auto& [id, b] : bodies_) {
            if (b.desc.type != BodyType::Static) continue;
            const glm::vec3 c = b.state.position + b.desc.shape.offset;
            if (b.desc.shape.type == ShapeType::Plane) {
                supports.push_back({ id, b.desc.userData, c.y, true, {}, {} });
            }
            else if (b.desc.shape.type == ShapeType::Box) {
                const glm::vec3 h = b.desc.shape.halfExtents;
                supports.push_back({ id, b.desc.userData, c.y + h.y, false, c - h, c + h });
            }
        }

        for (auto& [id, b] : bodies_) {
            if (b.desc.type != BodyType::Dynamic) continue;

            // semi-implicit Euler: velocity first, then position (stable at a
            // fixed dt, unlike explicit Euler)
            b.state.linearVelocity += settings_.gravity * fixedDt;
            const float damp = std::clamp(1.f - b.desc.linearDamping * fixedDt, 0.f, 1.f);
            b.state.linearVelocity *= damp;
            b.state.position += b.state.linearVelocity * fixedDt;

            // resting contact: stop at the highest support underneath
            const float bottom = shapeBottomOffset(b.desc.shape);
            const glm::vec3 p = b.state.position + b.desc.shape.offset;
            float restY = -std::numeric_limits<float>::infinity();
            const Support* restOn = nullptr;
            for (const auto& s : supports) {
                if (!s.infinite) {
                    if (p.x < s.min.x || p.x > s.max.x || p.z < s.min.z || p.z > s.max.z) {
                        continue; // outside this box's horizontal extent
                    }
                }
                if (s.topY > restY) { restY = s.topY; restOn = &s; }
            }
            uint64_t nowRestingOn = 0;
            float landingImpulse = 0.f;
            if (restY > -std::numeric_limits<float>::infinity()) {
                const float minCenterY = restY + bottom - b.desc.shape.offset.y;
                if (b.state.position.y <= minCenterY && b.state.linearVelocity.y <= 0.f) {
                    if (restOn) nowRestingOn = restOn->id;
                    // impulse needed to cancel the downward velocity
                    landingImpulse = std::fabs(b.state.linearVelocity.y) *
                                     ((b.desc.mass > 0.f) ? b.desc.mass : 1.f);
                    b.state.position.y = minCenterY;
                    const float e = b.desc.material.restitution;
                    if (e > 0.f && std::fabs(b.state.linearVelocity.y) > 0.5f) {
                        b.state.linearVelocity.y = -b.state.linearVelocity.y * e;
                    }
                    else {
                        b.state.linearVelocity.y = 0.f;
                        // friction bleeds off horizontal motion while resting
                        const float f = std::clamp(1.f - b.desc.material.friction * fixedDt * 10.f, 0.f, 1.f);
                        b.state.linearVelocity.x *= f;
                        b.state.linearVelocity.z *= f;
                    }
                }
            }

            // Begin/End transitions against the supporting body
            if (nowRestingOn != b.restingOn) {
                auto emit = [&](uint64_t otherId, ContactPhase phase) {
                    if (otherId == 0) return;
                    ContactEvent e;
                    e.phase = phase;
                    e.a = BodyId{ id };
                    e.b = BodyId{ otherId };
                    e.userDataA = b.desc.userData;
                    const auto it = bodies_.find(otherId);
                    e.userDataB = (it != bodies_.end()) ? it->second.desc.userData : 0ull;
                    e.normal = { 0.f, 1.f, 0.f };
                    e.point = b.state.position;
                    if (phase == ContactPhase::Begin) e.impulse = landingImpulse;
                    events_.push_back(e);
                };
                emit(b.restingOn, ContactPhase::End);   // left the old support
                emit(nowRestingOn, ContactPhase::Begin); // landed on a new one
                b.restingOn = nowRestingOn;
            }
        }
    }

    bool SimplePhysicsBackend::getBodyState(BodyId id, BodyState& out) const {
        const auto it = bodies_.find(id.value);
        if (it == bodies_.end()) return false;
        out = it->second.state;
        return true;
    }

    void SimplePhysicsBackend::setBodyTransform(BodyId id, const glm::vec3& position,
                                                const glm::quat& rotation) {
        const auto it = bodies_.find(id.value);
        if (it == bodies_.end()) return;
        it->second.state.position = position;
        it->second.state.rotation = rotation;
    }

    void SimplePhysicsBackend::setLinearVelocity(BodyId id, const glm::vec3& v) {
        const auto it = bodies_.find(id.value);
        if (it != bodies_.end()) it->second.state.linearVelocity = v;
    }

    void SimplePhysicsBackend::setAngularVelocity(BodyId id, const glm::vec3& v) {
        const auto it = bodies_.find(id.value);
        if (it != bodies_.end()) it->second.state.angularVelocity = v;
    }

    void SimplePhysicsBackend::applyImpulse(BodyId id, const glm::vec3& impulse) {
        const auto it = bodies_.find(id.value);
        if (it == bodies_.end()) return;
        auto& b = it->second;
        if (b.desc.type != BodyType::Dynamic) return;
        const float m = (b.desc.mass > 0.f) ? b.desc.mass : 1.f;
        b.state.linearVelocity += impulse / m;
    }

    void SimplePhysicsBackend::wakeBody(BodyId) {
        // nothing sleeps in this backend
    }

    bool SimplePhysicsBackend::raycast(const glm::vec3& origin, const glm::vec3& direction,
                                       float maxDistance, RayHit& out) const {
        out = RayHit{};
        const float len = glm::length(direction);
        if (len <= 1e-6f || maxDistance <= 0.f) return false;
        const glm::vec3 dir = direction / len;

        float best = maxDistance;
        for (const auto& [id, b] : bodies_) {
            const glm::vec3 c = b.state.position + b.desc.shape.offset;
            float t = -1.f;
            glm::vec3 n{ 0.f };

            switch (b.desc.shape.type) {
            case ShapeType::Plane: {
                // horizontal plane at c.y, normal +Y
                if (std::fabs(dir.y) < 1e-6f) break;
                t = (c.y - origin.y) / dir.y;
                n = glm::vec3(0.f, 1.f, 0.f);
                break;
            }
            case ShapeType::Sphere:
            case ShapeType::Capsule: { // capsule approximated by its bounding sphere
                const float r = (b.desc.shape.type == ShapeType::Sphere)
                    ? b.desc.shape.radius
                    : b.desc.shape.radius + b.desc.shape.halfHeight;
                const glm::vec3 m = origin - c;
                const float bq = glm::dot(m, dir);
                const float cq = glm::dot(m, m) - r * r;
                if (cq > 0.f && bq > 0.f) break;      // origin outside, pointing away
                const float disc = bq * bq - cq;
                if (disc < 0.f) break;
                t = -bq - std::sqrt(disc);
                if (t < 0.f) t = 0.f;
                n = glm::normalize((origin + dir * t) - c);
                break;
            }
            case ShapeType::Box: {
                // slab test
                const glm::vec3 h = b.desc.shape.halfExtents;
                const glm::vec3 lo = c - h, hi = c + h;
                float tmin = 0.f, tmax = maxDistance;
                bool miss = false;
                int axis = 0; float sign = 1.f;
                for (int i = 0; i < 3 && !miss; ++i) {
                    if (std::fabs(dir[i]) < 1e-6f) {
                        if (origin[i] < lo[i] || origin[i] > hi[i]) miss = true;
                        continue;
                    }
                    const float inv = 1.f / dir[i];
                    float t1 = (lo[i] - origin[i]) * inv;
                    float t2 = (hi[i] - origin[i]) * inv;
                    float s = -1.f;
                    if (t1 > t2) { std::swap(t1, t2); s = 1.f; }
                    if (t1 > tmin) { tmin = t1; axis = i; sign = s; }
                    tmax = std::min(tmax, t2);
                    if (tmin > tmax) miss = true;
                }
                if (miss) break;
                t = tmin;
                n = glm::vec3(0.f);
                n[axis] = sign;
                break;
            }
            }

            if (t >= 0.f && t < best) {
                best = t;
                out.hit = true;
                out.body = BodyId{ id };
                out.distance = t;
                out.point = origin + dir * t;
                out.normal = n;
                out.userData = b.desc.userData;
            }
        }
        return out.hit;
    }

} // namespace MyCoreEngine
