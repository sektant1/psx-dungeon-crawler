#include "script/bind/Bindings.h"

#include <eng/Audio.h>
#include <eng/AudioTypes.h>
#include <eng/Log.h>

#include <string>

namespace eng::script {
namespace {

// Bus by name, because a script naming AudioBus::Weapons by integer would be a
// number nobody can read and a renumbering nobody can find. Unknown names fall
// back to Sfx and say so once -- a typo'd bus should still make a sound, or
// somebody spends an afternoon on a silent gun.
AudioBus busFromName(const std::string& name)
{
    if (name.empty() || name == "sfx") return AudioBus::Sfx;
    if (name == "music") return AudioBus::Music;
    if (name == "ambience") return AudioBus::Ambience;
    if (name == "dialogue") return AudioBus::Dialogue;
    if (name == "weapons") return AudioBus::Weapons;
    if (name == "ui") return AudioBus::Ui;
    if (name == "warnings") return AudioBus::Warnings;
    if (name == "master") return AudioBus::Master;
    log::warn("Script: unknown audio bus '%s'; using sfx", name.c_str());
    return AudioBus::Sfx;
}

// The options table every play call takes. Absent keys keep PlaybackSettings'
// own defaults, so `sound.play(path)` and the C++ default are the same sound.
PlaybackSettings settingsFrom(const sol::optional<sol::table>& options)
{
    PlaybackSettings s;
    if (!options.has_value())
        return s;
    const sol::table& t = *options;
    s.bus = busFromName(t.get_or("bus", std::string{}));
    s.gainDb = t.get_or("gain_db", s.gainDb);
    s.pitch = t.get_or("pitch", s.pitch);
    s.loop = t.get_or("loop", s.loop);
    s.minDistance = t.get_or("min_distance", s.minDistance);
    s.maxDistance = t.get_or("max_distance", s.maxDistance);
    s.fadeInSeconds = t.get_or("fade_in", s.fadeInSeconds);
    return s;
}

} // namespace

void bindAudio(sol::state& lua, Audio& audio)
{
    sol::table t = lua.create_named_table("sound");

    // Fire and forget. No handle comes back on purpose: the overwhelming
    // majority of gameplay audio is a hit, a footstep or a pickup, and handing
    // every one of those a handle to leak is worse than not being able to stop
    // one. A script that needs to stop a sound wants a looping ambience, which
    // is what `sound.loop` below returns a stopper for.
    t["play"] = [&audio](const std::string& path,
                         sol::optional<sol::table> options) {
        return audio.playOneShot(path, settingsFrom(options));
    };

    // The same, positioned in the world. Spatialisation is implied by having
    // said where it is, rather than being a separate flag somebody forgets --
    // a positioned sound that plays flat is the bug this shape prevents.
    t["play_at"] = [&audio](const std::string& path, const glm::vec3& position,
                            sol::optional<sol::table> options) {
        PlaybackSettings s = settingsFrom(options);
        s.spatialized = true;
        s.position = position;
        return audio.playOneShot(path, s);
    };

    // A retained sound, for something that has to be stopped later: an
    // ambience, an engine hum, a charging weapon. Returns a table with stop()
    // rather than a bare handle, so the only thing a script can do with it is
    // the thing it asked for.
    t["loop"] = [&audio](const std::string& path,
                         sol::optional<sol::table> options,
                         sol::this_state ts) -> sol::object {
        PlaybackSettings s = settingsFrom(options);
        s.loop = true;
        SoundInstance::Ptr instance = audio.play(path, s);
        if (!instance)
            return sol::lua_nil;
        sol::state_view lv(ts);
        sol::table handle = lv.create_table();
        handle["stop"] = [instance]() { instance->stop(); };
        handle["playing"] = [instance]() { return instance->isPlaying(); };
        return handle;
    };

    // Mixer, by bus name. Normalised 0..1 rather than dB: a script setting a
    // music slider is doing UI, and dB is the mixing engineer's unit, not the
    // gameplay programmer's. The dB controls stay in C++ where the mixing is.
    t["bus_volume"] = [&audio](const std::string& bus, float normalized) {
        audio.setBusVolume(busFromName(bus), normalized);
    };
    t["mute_bus"] = [&audio](const std::string& bus, bool muted) {
        audio.setBusMuted(busFromName(bus), muted);
    };
}

} // namespace eng::script
