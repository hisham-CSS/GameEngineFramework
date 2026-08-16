// Engine/src/core/ProjectSettings.h
#pragma once
#include "Core.h"

#include <string>

namespace MyCoreEngine {

    // Launch settings shared between the editor and the player.
    // Stored as JSON next to the assets (Exported/project.json) so it ships
    // inside the packaged game bundle. The editor writes it (File > Set Current
    // Scene as Player Startup, and Settings > Audio for the master volume); the
    // player reads it at boot.
    //
    // THIS IS THE RUNTIME CONTRACT, AND IT STOPS BEING THE AUTHORING SURFACE.
    // `startupScene` was a build setting with a population of one: a game is a
    // menu, then a fight, then a results screen, and one field cannot say that.
    // The ORDERED list lives in BuildSettings (BuildSettings.h), which the editor
    // authors and which a shipped game never reads, and the arrow between them
    // runs one way:
    //
    //     build.json --(BuildSettings::ApplyTo, at Build time)--> project.json
    //
    // So `startupScene` becomes DERIVED -- the build writes it from
    // `BuildSettings::scenes[0]` -- and nothing here changes: the player goes on
    // reading it exactly as it does today (PlayerMain.cpp:200), and the boot
    // order documented there (command line, then a linked title's front end,
    // then this) is untouched.
    //
    // AND THE LIST DELIBERATELY DOES NOT LIVE IN THIS STRUCT. Save() rewrites the
    // file from the fields modelled here, and MenuUIContent.cpp:200-207 performs a
    // LOAD-MODIFY-SAVE of it from inside the RUNNING GAME when a player moves the
    // volume slider. A scene list in project.json would therefore be destroyed by
    // a player adjusting the volume in the shipped bundle -- silently, in the one
    // place nobody would look.
    struct ENGINE_API ProjectSettings {
        std::string startupScene = "Exported/scene.json";
        float masterVolume = 1.0f; // 0..1, scales the whole audio mix; the
                                   // editor writes it, the player boots at it

        // IS THIS FILE A BUILD MANIFEST, OR A PREFERENCE?
        //
        // False in every project.json that exists today, and true only in one an
        // editor Build wrote into a bundle. It is the flag ADR-008 decision 5
        // needs, and the invariant it buys is: `scenes[0]` IS WHAT THE BUILT GAME
        // BOOTS.
        //
        // Without it that invariant is unreachable for the title that exists.
        // PlayerMain.cpp:196-200 resolves the boot scene as command line, then
        // `TitleFrontEndScene()`, then this file -- so a build whose scenes[0] is
        // a level rather than the front end writes startupScene, is overruled at
        // boot, and produces a bundle that starts somewhere the panel did not
        // say. Silently.
        //
        // AND THE EXISTING PRECEDENCE IS EXACTLY RIGHT FOR THE UNMARKED CASE, so
        // it is left alone. PlayerMain.cpp:182-190 argues that project.json is a
        // HOST PREFERENCE predating titles whose default is the engine demo, so
        // letting it win would ship a game that boots the backpack room because
        // somebody once pressed a menu item. That argument is about files the
        // BUILD DID NOT WRITE, and this flag is precisely how a host tells the
        // two apart:
        //
        //   false -> today's order, unchanged.
        //   true  -> startupScene outranks TitleFrontEndScene(). The command line
        //            still wins, because it is the deliberate act.
        //
        // MODELLED HERE RATHER THAN LEFT AS AN UNKNOWN KEY, and that is not a
        // style choice. MenuUIContent.cpp:200-207 load-modify-saves this file from
        // inside the RUNNING GAME when a player moves the volume slider, and Save
        // rewrites it from the fields this struct models -- so a marker the struct
        // did not know about would be erased by a volume change, and the bundle
        // would silently revert to booting the title's front end. A field that
        // Load reads and Save writes survives that round trip.
        bool startupSceneFromBuild = false;

        static const char* DefaultPath() { return "Exported/project.json"; }

        // Missing file is not an error: defaults stand. Malformed JSON logs
        // and returns false, keeping defaults.
        bool Load(const std::string& path = DefaultPath());
        bool Save(const std::string& path = DefaultPath()) const;
    };

} // namespace MyCoreEngine
