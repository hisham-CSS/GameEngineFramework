// tests/test_event_bus.cpp
#include "Engine.h"
#include <gtest/gtest.h>
using namespace MyCoreEngine;

TEST(EventBus, PublishesAndReceives) {
	int resize = 0, move = 0, scroll = 0;
	auto& bus = EventBus::Get();
	int rId = bus.subscribe([&](const WindowResizeEvent&) { ++resize; });
	int mId = bus.subscribe([&](const MouseMoveEvent&) { ++move; });
	int sId = bus.subscribe([&](const MouseScrollEvent&) { ++scroll; });

	bus.publish(WindowResizeEvent{ 1280,720 });
	bus.publish(MouseMoveEvent{ 100,200 });
	bus.publish(MouseScrollEvent{ 1 });

	EXPECT_EQ(resize, 1);
	EXPECT_EQ(move, 1);
	EXPECT_EQ(scroll, 1);

	bus.unsubscribeResize(rId);
	bus.unsubscribeMouseMove(mId);
	bus.unsubscribeMouseScroll(sId);
}
