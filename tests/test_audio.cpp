// Audio: the backend seam + registry. Headless by design — the Miniaudio
// backend may fail to open a device in CI (no sound card), which is a
// first-class supported outcome, so the suite asserts graceful behaviour, not
// that audio actually plays.
#include <gtest/gtest.h>

#include "Engine.h"

#include <glm/gtc/matrix_transform.hpp> // translate, for building world matrices

#include <algorithm>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

using namespace MyCoreEngine;

namespace {

bool has(const std::vector<std::string>& v, const char* s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

// A FUNCTIONAL headless backend, the audio equivalent of physics' "Simple".
//
// The Null backend's play() returns 0 unconditionally, so `if (id) active_[e] =
// id;` never fires and the entity->voice map stays empty for the whole test:
// on Null you can only prove "nothing crashes". The playOnStart filter, the
// entity->voice map, per-frame setPosition on spatial sources, the finished-
// voice reaper, Clear-on-restart and listener selection all had ZERO effective
// coverage — deleting the playOnStart check or the Clear() in Start left the
// suite green. This backend hands out real ids and records what it was told,
// so those behaviours can actually be asserted.
struct FakeVoice {
    std::string clip;
    SoundParams params;
    glm::vec3 position{ 0.f };
    bool playing = true;
};

class FakeAudioBackend : public IAudioBackend {
public:
    // Shared with the test body: the backend is owned by AudioWorld, so state
    // has to outlive the instance to be inspectable.
    struct State {
        std::unordered_map<SoundId, FakeVoice> voices;
        SoundId next = 1;
        int stopAllCalls = 0;
        glm::vec3 listenerPos{ 0.f }, listenerFwd{ 0.f }, listenerUp{ 0.f };
        float masterVolume = 1.f;
        std::vector<std::string> played; // clips, in play order
    };
    static State* state;

    const char* name() const override { return "Fake"; }
    bool initialize(const AudioSettings& s) override {
        if (state) state->masterVolume = s.masterVolume;
        return true;
    }
    void shutdown() override {}
    SoundId play(const std::string& path, const SoundParams& p) override {
        if (!state || path.empty()) return 0;
        const SoundId id = state->next++;
        state->voices[id] = FakeVoice{ path, p, p.position, true };
        state->played.push_back(path);
        return id;
    }
    void stop(SoundId id) override { if (state) state->voices.erase(id); }
    void stopAll() override {
        if (!state) return;
        ++state->stopAllCalls;
        state->voices.clear();
    }
    bool isPlaying(SoundId id) const override {
        if (!state) return false;
        auto it = state->voices.find(id);
        return it != state->voices.end() && it->second.playing;
    }
    void setPosition(SoundId id, const glm::vec3& p) override {
        if (!state) return;
        auto it = state->voices.find(id);
        if (it != state->voices.end()) it->second.position = p;
    }
    void setVolume(SoundId, float) override {}
    void setPitch(SoundId, float) override {}
    void setListener(const glm::vec3& p, const glm::vec3& f, const glm::vec3& u) override {
        if (!state) { return; }
        state->listenerPos = p; state->listenerFwd = f; state->listenerUp = u;
    }
    void setMasterVolume(float v) override { if (state) state->masterVolume = v; }
    void update() override {}
};
FakeAudioBackend::State* FakeAudioBackend::state = nullptr;

// Fixture: registers the fake and points it at per-test state.
class AudioWorldTest : public ::testing::Test {
protected:
    FakeAudioBackend::State st;
    void SetUp() override {
        FakeAudioBackend::state = &st;
        RegisterBuiltinAudioBackends();
        AudioBackendRegistry::Register("Fake", [] {
            return std::unique_ptr<IAudioBackend>(new FakeAudioBackend());
        });
    }
    void TearDown() override { FakeAudioBackend::state = nullptr; }

    static entt::entity makeSource(entt::registry& reg, const char* clip,
                                   bool playOnStart, bool spatial,
                                   glm::vec3 pos = glm::vec3(0.f)) {
        auto e = reg.create();
        Transform t{};
        t.position = pos;
        t.modelMatrix = glm::translate(glm::mat4(1.f), pos);
        reg.emplace<Transform>(e, t);
        auto& s = reg.emplace<AudioSourceComponent>(e);
        s.clip = clip;
        s.playOnStart = playOnStart;
        s.spatial = spatial;
        return e;
    }
};

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

// ---- AudioWorld <-> ECS, on a backend that actually hands out voices -------

TEST_F(AudioWorldTest, StartPlaysOnlyPlayOnStartSources) {
    AudioWorld world;
    ASSERT_TRUE(world.SetBackend("Fake"));
    entt::registry reg;
    makeSource(reg, "auto.wav",   /*playOnStart*/true,  /*spatial*/true);
    makeSource(reg, "manual.wav", /*playOnStart*/false, /*spatial*/true);

    world.Start(reg);

    ASSERT_EQ(st.played.size(), 1u) << "playOnStart filter ignored";
    EXPECT_EQ(st.played[0], "auto.wav");
}

TEST_F(AudioWorldTest, RestartDoesNotStackDuplicateVoices) {
    AudioWorld world;
    ASSERT_TRUE(world.SetBackend("Fake"));
    entt::registry reg;
    makeSource(reg, "loop.wav", true, true);

    world.Start(reg);
    const int afterFirst = (int)st.voices.size();
    world.Start(reg); // Start() must Clear() first

    EXPECT_EQ(afterFirst, 1);
    EXPECT_EQ((int)st.voices.size(), 1) << "a second Start stacked a duplicate voice";
    EXPECT_GE(st.stopAllCalls, 1) << "Start did not clear before replaying";
}

TEST_F(AudioWorldTest, SpatialSourcesTrackTheirTransform) {
    AudioWorld world;
    ASSERT_TRUE(world.SetBackend("Fake"));
    entt::registry reg;
    auto moving = makeSource(reg, "engine.wav", true, /*spatial*/true, { 1.f, 2.f, 3.f });
    auto flat   = makeSource(reg, "music.wav",  true, /*spatial*/false, { 9.f, 9.f, 9.f });

    world.Start(reg);
    // Move the entity, as gameplay would.
    reg.get<Transform>(moving).modelMatrix = glm::translate(glm::mat4(1.f), { 5.f, 6.f, 7.f });
    Camera cam;
    world.Update(reg, cam);

    // Find the voices by clip (ids are backend-internal).
    const FakeVoice* movingVoice = nullptr;
    const FakeVoice* flatVoice = nullptr;
    for (const auto& [id, v] : st.voices) {
        if (v.clip == "engine.wav") movingVoice = &v;
        if (v.clip == "music.wav")  flatVoice = &v;
    }
    ASSERT_NE(movingVoice, nullptr);
    ASSERT_NE(flatVoice, nullptr);
    EXPECT_FLOAT_EQ(movingVoice->position.x, 5.f) << "3D source did not follow its transform";
    EXPECT_FLOAT_EQ(movingVoice->position.z, 7.f);
    // A 2D source is position-independent; the world must not push one.
    EXPECT_FLOAT_EQ(flatVoice->position.x, 9.f);
}

TEST_F(AudioWorldTest, ListenerPrefersTheListenerComponentOverTheCamera) {
    AudioWorld world;
    ASSERT_TRUE(world.SetBackend("Fake"));
    entt::registry reg;
    Camera cam;
    cam.Position = { 100.f, 100.f, 100.f };

    // No listener entity: falls back to the camera.
    world.Update(reg, cam);
    EXPECT_FLOAT_EQ(st.listenerPos.x, 100.f) << "camera fallback not used";

    // With one, it wins.
    auto ears = reg.create();
    Transform t{};
    t.modelMatrix = glm::translate(glm::mat4(1.f), { 4.f, 5.f, 6.f });
    reg.emplace<Transform>(ears, t);
    reg.emplace<AudioListenerComponent>(ears);
    world.Update(reg, cam);
    EXPECT_FLOAT_EQ(st.listenerPos.x, 4.f) << "AudioListenerComponent did not override the camera";
    EXPECT_FLOAT_EQ(st.listenerPos.z, 6.f);
}

// Regression for the orphaned-voice fix: a LOOPING voice is always "playing",
// so the finished-voice reaper alone never released it — deleting the entity
// mid-play left the sound running forever.
TEST_F(AudioWorldTest, VoiceStopsWhenItsEntityGoesAway) {
    AudioWorld world;
    ASSERT_TRUE(world.SetBackend("Fake"));
    entt::registry reg;
    auto e = makeSource(reg, "siren.wav", true, true);
    reg.get<AudioSourceComponent>(e).loop = true;

    world.Start(reg);
    ASSERT_EQ((int)st.voices.size(), 1);

    reg.destroy(e); // as the Hierarchy's Delete Entity does, mid-play
    Camera cam;
    world.Update(reg, cam);

    EXPECT_EQ((int)st.voices.size(), 0)
        << "a looping voice outlived its entity and plays forever";
}

TEST_F(AudioWorldTest, RemovingTheComponentStopsItsVoice) {
    AudioWorld world;
    ASSERT_TRUE(world.SetBackend("Fake"));
    entt::registry reg;
    auto e = makeSource(reg, "siren.wav", true, true);
    reg.get<AudioSourceComponent>(e).loop = true;

    world.Start(reg);
    ASSERT_EQ((int)st.voices.size(), 1);

    reg.remove<AudioSourceComponent>(e); // Inspector's remove-component X
    Camera cam;
    world.Update(reg, cam);

    EXPECT_EQ((int)st.voices.size(), 0) << "voice survived removal of its component";
}

TEST_F(AudioWorldTest, ClearStopsEverything) {
    AudioWorld world;
    ASSERT_TRUE(world.SetBackend("Fake"));
    entt::registry reg;
    makeSource(reg, "a.wav", true, true);
    makeSource(reg, "b.wav", true, true);

    world.Start(reg);
    ASSERT_EQ((int)st.voices.size(), 2);
    world.Clear();
    EXPECT_EQ((int)st.voices.size(), 0) << "Stop left voices playing";
}

// An unknown backend must fail AND leave a usable (Null) world behind, not a
// half-initialised one.
TEST_F(AudioWorldTest, UnknownBackendFallsBackToSomethingUsable) {
    AudioWorld world;
    EXPECT_FALSE(world.SetBackend("NoSuchBackend"));
    EXPECT_TRUE(world.HasBackend()) << "a failed backend switch left no backend at all";
    EXPECT_EQ(world.BackendName(), "Null");

    entt::registry reg;
    Camera cam;
    world.Start(reg);
    world.Update(reg, cam);  // must not crash
    world.Clear();
}

} // namespace
