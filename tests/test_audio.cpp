// Audio: the backend seam + registry. Headless by design — the Miniaudio
// backend may fail to open a device in CI (no sound card), which is a
// first-class supported outcome, so the suite asserts graceful behaviour, not
// that audio actually plays.
#include <gtest/gtest.h>

#include "Engine.h"

#include <algorithm>

using namespace MyCoreEngine;

namespace {

bool has(const std::vector<std::string>& v, const char* s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

TEST(Audio, RegistryListsBuiltins) {
    AudioBackendRegistry::Clear();
    RegisterBuiltinAudioBackends();
    const auto avail = AudioBackendRegistry::Available();
    EXPECT_TRUE(has(avail, "Null"));
    EXPECT_TRUE(has(avail, "Miniaudio"));
    EXPECT_TRUE(AudioBackendRegistry::IsRegistered("Null"));
    EXPECT_FALSE(AudioBackendRegistry::IsRegistered("Nope"));
    EXPECT_EQ(AudioBackendRegistry::Create("Nope"), nullptr);
    EXPECT_STREQ(DefaultAudioBackendName(), "Miniaudio");
}

TEST(Audio, NullBackendIsSilentButValid) {
    RegisterBuiltinAudioBackends();
    auto a = AudioBackendRegistry::Create("Null");
    ASSERT_NE(a, nullptr);
    EXPECT_TRUE(a->initialize({}));           // never fails
    SoundParams p; p.spatial = true;
    EXPECT_EQ(a->play("whatever.wav", p), 0u); // no id, no device
    EXPECT_FALSE(a->isPlaying(1));
    a->setListener({0,0,0}, {0,0,-1}, {0,1,0});
    a->setMasterVolume(0.5f);
    a->update();
    a->shutdown();                             // safe with nothing playing
}

TEST(Audio, MiniaudioInitIsGracefulWithOrWithoutDevice) {
    RegisterBuiltinAudioBackends();
    auto a = AudioBackendRegistry::Create("Miniaudio");
    ASSERT_NE(a, nullptr);
    // initialize() returns true with a device, false without — both fine. The
    // object must stay safely usable/destructible either way.
    const bool up = a->initialize({});
    a->setMasterVolume(0.8f);
    a->setListener({1,2,3}, {0,0,-1}, {0,1,0});
    EXPECT_EQ(a->play("nonexistent-file.wav", {}), 0u); // missing file => no id
    a->update();
    a->stopAll();
    a->shutdown();
    SUCCEED() << "miniaudio init returned " << (up ? "device-ready" : "no-device");
}

// AudioWorld drives sources/listener from the ECS. On the Null backend (always
// available, headless) the whole lifecycle must run without crashing.
TEST(Audio, WorldLifecycleOnNullBackend) {
    AudioWorld world;
    EXPECT_TRUE(world.SetBackend("Null"));
    EXPECT_EQ(world.BackendName(), "Null");
    EXPECT_TRUE(world.HasBackend());

    entt::registry reg;

    auto src = reg.create();
    reg.emplace<Transform>(src);
    auto& s = reg.emplace<AudioSourceComponent>(src);
    s.clip = "sfx.wav"; s.spatial = true; s.playOnStart = true;

    auto listener = reg.create();
    reg.emplace<Transform>(listener);
    reg.emplace<AudioListenerComponent>(listener);

    Camera cam;
    world.Start(reg);          // Null.play -> 0, so no active voice tracked
    world.Update(reg, cam);    // listener from the AudioListenerComponent entity
    world.SetMasterVolume(0.5f);
    EXPECT_EQ(world.PlayOneShot("boom.wav", { 1, 2, 3 }), 0u); // Null -> no id
    world.Clear();
    SUCCEED();
}

} // namespace
