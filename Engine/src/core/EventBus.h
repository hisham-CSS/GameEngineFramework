// EventBus.h
#pragma once
#include <functional>
#include <vector>
#include <algorithm>
#include "Event.h"

namespace MyCoreEngine {

    // Minimal, synchronous, single-threaded event bus.
    // Overloads per event type keep it simple & header-only.
    class EventBus {
    public:
        using WindowResizeListener = std::function<void(const WindowResizeEvent&)>;
        using MouseMoveListener = std::function<void(const MouseMoveEvent&)>;
        using MouseScrollListener = std::function<void(const MouseScrollEvent&)>;
        using KeyListener = std::function<void(const KeyEvent&)>;

        static EventBus& Get() {
            static EventBus instance;
            return instance;
        }

        // Subscribe
        int subscribe(const WindowResizeListener& f) { return add(listResize_, f); }
        int subscribe(const MouseMoveListener& f) { return add(listMouseMove_, f); }
        int subscribe(const MouseScrollListener& f) { return add(listMouseScroll_, f); }
        int subscribe(const KeyListener& f) { return add(listKey_, f); }

        // Unsubscribe
        void unsubscribeResize(int id) { remove(listResize_, id); }
        void unsubscribeMouseMove(int id) { remove(listMouseMove_, id); }
        void unsubscribeMouseScroll(int id) { remove(listMouseScroll_, id); }
        void unsubscribeKey(int id) { remove(listKey_, id); }

        // Publish
        void publish(const WindowResizeEvent& e) { dispatch(listResize_, e); }
        void publish(const MouseMoveEvent& e) { dispatch(listMouseMove_, e); }
        void publish(const MouseScrollEvent& e) { dispatch(listMouseScroll_, e); }
        void publish(const KeyEvent& e) { dispatch(listKey_, e); }

    private:
        template<typename F>
        struct ListenerRec { int id; F fn; };

        template<typename F>
        int add(std::vector<ListenerRec<F>>& v, const F& f) {
            int id = ++counter_;
            v.push_back({ id, f });
            return id;
        }
        template<typename F>
        void remove(std::vector<ListenerRec<F>>& v, int id) {
            v.erase(std::remove_if(v.begin(), v.end(),
                [id](const auto& r) { return r.id == id; }),
                v.end());
        }
        template<typename F, typename E>
        void dispatch(const std::vector<ListenerRec<F>>& v, const E& e) {
            for (const auto& r : v) r.fn(e);
        }

        int counter_ = 0;
        std::vector<ListenerRec<WindowResizeListener>> listResize_;
        std::vector<ListenerRec<MouseMoveListener>>    listMouseMove_;
        std::vector<ListenerRec<MouseScrollListener>>  listMouseScroll_;
        std::vector<ListenerRec<KeyListener>>          listKey_;
    };

} // namespace MyCoreEngine
