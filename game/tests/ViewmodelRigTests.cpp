#include "ViewmodelMotion.h"
#include "TestAssets.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>
#include <iostream>
#include <limits>
#include <string>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "ViewmodelRigTests: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

bool near(float a, float b, float epsilon = 0.0005f)
{
    return std::abs(a - b) <= epsilon;
}

// Steps the composer for `seconds` at 60 Hz with a constant input.
game::ViewmodelPose run(game::ViewmodelMotion& motion,
                        game::ViewmodelMotionInput input, float seconds)
{
    game::ViewmodelPose pose;
    input.dt = 1.0f / 60.0f;
    for (float t = 0.0f; t < seconds; t += input.dt)
        pose = motion.update(input);
    return pose;
}

} // namespace

int main()
{
    game::test::mountGameAssets();
    using namespace game;

    // --- the shipped section loads and matches the rig's built-in framing ---
    ViewmodelRig shipped;
    require(loadViewmodelRig(game::test::asset("config/game.toml"),
                                   shipped),
            "shipped [player_viewmodel] did not load");
    require(validViewmodelRig(shipped), "shipped rig tuning is invalid");
    // Against the struct's own defaults rather than against literals: the two
    // are the same framing authored twice (game.toml is what ships, the struct
    // is what a level with no rig gets), and the failure worth catching is them
    // drifting apart -- not the numbers changing, which is tuning.
    {
        const ViewmodelRig builtin;
        require(near(shipped.offset.x, builtin.offset.x) &&
                    near(shipped.offset.y, builtin.offset.y) &&
                    near(shipped.offset.z, builtin.offset.z) &&
                    near(shipped.rotation.x, builtin.rotation.x) &&
                    near(shipped.rotation.y, builtin.rotation.y) &&
                    near(shipped.rotation.z, builtin.rotation.z) &&
                    near(shipped.scale, builtin.scale),
                "shipped socket no longer matches the rig's built-in framing");
    }

    // --- saving ------------------------------------------------------------
    // The panel's Save writes into a file full of authored comments. Round-trip
    // it in a copy: what comes back must be what went in, and everything the
    // file said around the section must still be there.
    {
        const std::filesystem::path scratch =
            std::filesystem::temp_directory_path() / "raven_rig_save_test.toml";
        {
            std::ofstream out(scratch, std::ios::trunc);
            out << "# a note above\n"
                   "[player]\n"
                   "speed = 3.0\n"
                   "\n"
                   "[player_viewmodel]\n"
                   "# how low the hands sit\n"
                   "offset = [0.0, -0.95, -0.75]\n"
                   "scale = 0.5  # trailing note\n"
                   "\n"
                   "[audio]\n"
                   "master_db = -4.0\n";
        }

        ViewmodelRig edited;
        edited.offset = {0.25f, -0.80f, -0.10f};
        edited.rotation = {1.0f, 175.0f, 2.0f};
        edited.scale = 0.75f;
        edited.motionEnabled = false;
        require(saveViewmodelRig(scratch.string(), edited),
                "saving the rig into a real file failed");

        ViewmodelRig reloaded;
        require(loadViewmodelRig(scratch.string(), reloaded),
                "the saved file no longer parses");
        require(near(reloaded.offset.x, 0.25f) &&
                    near(reloaded.offset.y, -0.80f) &&
                    near(reloaded.offset.z, -0.10f) &&
                    near(reloaded.scale, 0.75f) &&
                    near(reloaded.rotation.y, 175.0f) &&
                    reloaded.motionEnabled == false,
                "the saved rig did not round-trip");

        std::string text;
        {
            std::ifstream in(scratch);
            text.assign(std::istreambuf_iterator<char>(in),
                        std::istreambuf_iterator<char>());
        }
        require(text.find("# a note above") != std::string::npos &&
                    text.find("[player]") != std::string::npos &&
                    text.find("speed = 3.0") != std::string::npos &&
                    text.find("[audio]") != std::string::npos &&
                    text.find("master_db = -4.0") != std::string::npos,
                "saving the rig disturbed the rest of the file");
        require(text.find("# how low the hands sit") != std::string::npos &&
                    text.find("# trailing note") != std::string::npos,
                "saving the rig deleted the section's own comments");
        // A key the file did not carry is appended inside the section, not
        // dropped and not written after [audio].
        require(text.find("sway_max") != std::string::npos &&
                    text.find("sway_max") < text.find("[audio]"),
                "a missing key was not appended inside the section");
        std::error_code cleanup;
        std::filesystem::remove(scratch, cleanup);
    }

    // A missing section is not an error: the defaults are the shipped framing.
    ViewmodelRig untouched;
    require(parseViewmodelRig("[player]\nspeed = 3.0\n", untouched),
            "a document without the section should parse");
    require(near(untouched.scale, 0.5f), "absent section overwrote the default");

    // A bad value rejects the whole table rather than applying half of it.
    ViewmodelRig guarded;
    guarded.scale = 0.5f;
    require(!parseViewmodelRig(
                "[player_viewmodel]\nscale = -1.0\nbob_scale = 3.0\n", guarded),
            "negative scale was accepted");
    require(near(guarded.scale, 0.5f) && near(guarded.bobScale, 1.0f),
            "a rejected table was partially applied");

    // --- layer composition -------------------------------------------------
    ViewmodelMotion motion;
    ViewmodelRig tuning = shipped;
    motion.setTuning(tuning);

    WeaponViewmodelDef weapon;
    weapon.recoilDistance = 0.10f;
    weapon.recoilPitchDegrees = 8.0f;
    weapon.recoilRecovery = 20.0f;
    weapon.movementBob = 0.02f;
    weapon.movementBobSpeed = 8.0f;
    weapon.idleSway = 0.005f;
    weapon.lookSway = 0.001f;
    motion.setFeel(viewmodelFeel(weapon));

    // Standing still with motion frozen: the socket, exactly.
    tuning.motionEnabled = false;
    motion.setTuning(tuning);
    const ViewmodelPose frozen = run(motion, {}, 0.5f);
    require(near(frozen.position.x, shipped.offset.x) &&
                near(frozen.position.y, shipped.offset.y) &&
                near(frozen.position.z, shipped.offset.z) &&
                near(frozen.rotationDegrees.y, 180.0f) &&
                near(frozen.scale, 0.5f),
            "frozen rig did not hold the authored socket");

    // Weapon lean is added to the socket and survives the freeze: it is
    // placement, not motion.
    weapon.handsOffset = {0.05f, -0.02f, 0.03f};
    weapon.handsRotationDegrees = {0.0f, -6.0f, 0.0f};
    weapon.handsScale = 1.2f;
    motion.setFeel(viewmodelFeel(weapon));
    const ViewmodelPose leaned = run(motion, {}, 0.1f);
    require(near(leaned.position.x, shipped.offset.x + 0.05f) &&
                near(leaned.rotationDegrees.y, 174.0f) &&
                near(leaned.scale, 0.5f * 1.2f),
            "per-weapon lean did not compose onto the rig socket");

    tuning.motionEnabled = true;
    motion.setTuning(tuning);

    // Movement bob only moves the rig while the player is actually moving.
    ViewmodelMotionInput still;
    still.horizontalSpeed = 0.0f;
    still.grounded = true;
    ViewmodelMotionInput running = still;
    running.horizontalSpeed = 6.0f;

    motion.reset();
    float restSpan = 0.0f;
    float runSpan = 0.0f;
    {
        ViewmodelMotion probe;
        probe.setTuning(tuning);
        probe.setFeel(viewmodelFeel(weapon));
        float minX = 1e9f, maxX = -1e9f;
        for (int i = 0; i < 240; ++i) {
            still.dt = 1.0f / 60.0f;
            const ViewmodelPose p = probe.update(still);
            minX = std::min(minX, p.position.x);
            maxX = std::max(maxX, p.position.x);
        }
        restSpan = maxX - minX;
    }
    {
        ViewmodelMotion probe;
        probe.setTuning(tuning);
        probe.setFeel(viewmodelFeel(weapon));
        float minX = 1e9f, maxX = -1e9f;
        for (int i = 0; i < 240; ++i) {
            running.dt = 1.0f / 60.0f;
            const ViewmodelPose p = probe.update(running);
            minX = std::min(minX, p.position.x);
            maxX = std::max(maxX, p.position.x);
        }
        runSpan = maxX - minX;
    }
    require(runSpan > restSpan * 3.0f,
            "movement bob did not respond to player speed");
    require(restSpan > 0.0f && restSpan < 0.02f,
            "idle sway is either dead or as loud as the walk cycle");

    // Recoil kicks toward the eye (+z in camera space) and settles back.
    motion.reset();
    motion.kick();
    ViewmodelMotionInput firing = still;
    firing.dt = 1.0f / 60.0f;
    const ViewmodelPose kicked = motion.update(firing);
    require(kicked.position.z > shipped.offset.z + weapon.handsOffset.z + 0.02f,
            "firing recoil did not push the rig toward the camera");
    require(kicked.rotationDegrees.x > 1.0f, "firing recoil had no pitch");
    const ViewmodelPose settled = run(motion, firing, 1.0f);
    require(motion.recoil() < 0.01f &&
                near(settled.position.z,
                     shipped.offset.z + weapon.handsOffset.z, 0.005f),
            "recoil did not recover to the socket");

    // Repeated fire saturates instead of accumulating off screen.
    motion.reset();
    for (int i = 0; i < 50; ++i)
        motion.kick();
    require(motion.recoil() <= 1.4f, "recoil accumulated without a ceiling");

    // Look sway lags the view, stays clamped, and returns to centre.
    motion.reset();
    ViewmodelMotionInput flick = still;
    flick.dt = 1.0f / 60.0f;
    flick.lookDelta = {900.0f, 0.0f};
    motion.update(flick);
    require(motion.swayOffset().x <= 0.0f &&
                std::abs(motion.swayOffset().x) <= tuning.swayMax + 0.0001f,
            "look sway did not lag the view within its clamp");
    flick.lookDelta = {0.0f, 0.0f};
    run(motion, flick, 1.5f);
    require(std::abs(motion.swayOffset().x) < 0.002f,
            "look sway did not return to centre");

    // Landing dips once on touchdown, then recovers.
    motion.reset();
    ViewmodelMotionInput air = still;
    air.grounded = false;
    run(motion, air, 0.3f);
    ViewmodelMotionInput land = still;
    land.dt = 1.0f / 60.0f;
    const ViewmodelPose touchdown = motion.update(land);
    require(touchdown.position.y <
                shipped.offset.y + weapon.handsOffset.y - 0.02f,
            "landing did not dip the rig");
    run(motion, land, 1.5f);
    require(motion.landing() < 0.01f, "landing dip never recovered");

    // A non-finite delta must not poison the accumulators.
    motion.reset();
    ViewmodelMotionInput broken = running;
    broken.dt = std::numeric_limits<float>::quiet_NaN();
    broken.lookDelta = {std::numeric_limits<float>::infinity(), 0.0f};
    const ViewmodelPose safe = motion.update(broken);
    require(std::isfinite(safe.position.x) && std::isfinite(safe.position.y) &&
                std::isfinite(safe.position.z) && std::isfinite(safe.scale),
            "non-finite input produced a non-finite pose");

    // --- round trip: what the panel copies is what the loader reads back ---
    ViewmodelRig edited = shipped;
    edited.offset = {0.12f, -0.88f, -0.66f};
    edited.rotation = {-3.0f, 174.0f, 1.5f};
    edited.scale = 0.62f;
    edited.bobScale = 1.75f;
    edited.motionEnabled = false;
    ViewmodelRig reloaded;
    require(parseViewmodelRig(viewmodelRigToml(edited).c_str(), reloaded),
            "the panel's own TOML did not parse");
    require(near(reloaded.offset.x, edited.offset.x) &&
                near(reloaded.rotation.y, edited.rotation.y) &&
                near(reloaded.scale, edited.scale) &&
                near(reloaded.bobScale, edited.bobScale) &&
                reloaded.motionEnabled == edited.motionEnabled,
            "rig tuning did not survive a copy/paste round trip");

    // The weapon block the panel copies has to survive the weapon loader too.
    WeaponViewmodelDef weaponBlock = weapon;
    const std::string block = viewmodelWeaponToml("probe_weapon", weaponBlock);
    require(block.find("[player_weapon.probe_weapon.viewmodel]") == 0 &&
                block.find("hands_offset = [0.0500") != std::string::npos &&
                block.find("hands_scale = 1.2000") != std::string::npos,
            "weapon viewmodel block is not in the authored schema");

    std::cout << "ViewmodelRigTests: ok\n";
    return 0;
}
