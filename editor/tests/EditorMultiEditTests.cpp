// Multi-object property editing: Gregory §15.4.1.6, as arithmetic.
//
// The inspector draws one entity and the fan-out here is what makes that
// equivalent to the chapter's N-valued grid. The interesting property is
// negative: a field the author did NOT touch must not travel, or selecting
// forty pillars and nudging one axis silently flattens their other two.

#include <editor/scene/MultiEdit.h>

#include <iostream>
#include <string>

using namespace game::content;
namespace multiedit = ed::multiedit;

static int gFailures = 0;

static void check(bool condition, const std::string& what)
{
    if (!condition) {
        std::cerr << "EditorMultiEditTests: " << what << '\n';
        ++gFailures;
    }
}

static Entity pillar(const std::string& id, float x, float y)
{
    Entity entity;
    entity.id = id;
    entity.prefab = "kit.pillar";
    entity.transform.position = {x, y, 0.0f};
    return entity;
}

// One axis moved; the other two, and everything else, stay where they were.
static void testOneAxisTravelsAlone()
{
    const Entity before = pillar("a", 0.0f, 0.0f);
    Entity after = before;
    after.transform.position.y = 3.0f;

    Entity target = pillar("b", 10.0f, 7.0f);
    const Entity untouched = target;

    check(multiedit::applyDelta(before, after, target), "the edit landed");
    check(target.transform.position.y == 3.0f, "the edited axis was applied");
    check(target.transform.position.x == untouched.transform.position.x,
          "x was left alone");
    check(target.transform.position.z == untouched.transform.position.z,
          "z was left alone");
    check(target.transform.scale == untouched.transform.scale,
          "scale was left alone");
    check(target.id == "b", "the target keeps its own id");

    const std::vector<std::string> fields =
        multiedit::changedFields(before, after);
    check(fields.size() == 1 && fields.front() == "position.y",
          "the change is named for the undo label");
}

// A drag that ends where it began is not an edit, and must not fan anything out.
static void testNoOpDoesNotTravel()
{
    const Entity before = pillar("a", 2.0f, 2.0f);
    Entity after = before;
    // Below the writer's 4-decimal canonical resolution: the file would not
    // record this, so neither should the fan-out.
    after.transform.position.x += 0.000001f;

    Entity target = pillar("b", 9.0f, 9.0f);
    check(!multiedit::applyDelta(before, after, target),
          "a sub-resolution move is not an edit");
    check(target.transform.position.x == 9.0f, "the target did not move");
    check(multiedit::changedFields(before, after).empty(),
          "and nothing is named as changed");
}

// A material override needs a surface. A mixed selection takes the transform
// and ignores the rest, which is the chapter's heterogeneous-selection rule.
static void testMaterialNeedsAPrefab()
{
    Entity before = pillar("a", 0.0f, 0.0f);
    Entity after = before;
    after.material = "Game/Kit/Stone";
    after.transform.position.x = 5.0f;

    Entity withPrefab = pillar("b", 0.0f, 0.0f);
    check(multiedit::applyDelta(before, after, withPrefab), "it landed");
    check(withPrefab.material == "Game/Kit/Stone",
          "a prefab entity took the material");

    Entity bare;
    bare.id = "c"; // no prefab: nothing to override
    check(multiedit::applyDelta(before, after, bare), "the transform landed");
    check(bare.material.empty(), "a bare entity did not take the material");
    check(bare.transform.position.x == 5.0f,
          "but it did take the transform edit");
}

// "Make these thirty torches dimmer" is a lighting pass. The phase is the one
// light field that must NOT travel -- it is the per-instance offset that stops
// a wall of torches guttering in lockstep.
static void testLightFieldsFanOutExceptPhase()
{
    Entity before;
    before.id = "a";
    before.light = LightAuthor{};
    before.light->range = 8.0f;
    before.light->animation = LightAnimAuthor{};
    before.light->animation->phase = 0.0f;

    Entity after = before;
    after.light->range = 3.0f;
    after.light->animation->speed = 11.0f;

    Entity target;
    target.id = "b";
    target.light = LightAuthor{};
    target.light->range = 8.0f;
    target.light->animation = LightAnimAuthor{};
    target.light->animation->phase = 4.2f; // its own offset

    check(multiedit::applyDelta(before, after, target), "the light edit landed");
    check(target.light->range == 3.0f, "range fanned out");
    check(target.light->animation->speed == 11.0f, "animation speed fanned out");
    check(target.light->animation->phase == 4.2f,
          "the per-instance phase was preserved");

    // An entity that is not a light absorbs nothing from a light edit.
    Entity wall = pillar("c", 0.0f, 0.0f);
    check(!multiedit::applyDelta(before, after, wall),
          "a non-light ignores a light-only edit");
}

static void testAgreement()
{
    Entity a = pillar("a", 1.0f, 5.0f);
    Entity b = pillar("b", 2.0f, 5.0f); // differs on x, agrees on y and z
    Entity c = pillar("c", 3.0f, 5.0f);

    const std::vector<const Entity*> all = {&a, &b, &c};
    const multiedit::TransformAgreement agree = multiedit::agreementOf(all);
    check(!agree.position[0], "x is reported as mixed");
    check(agree.position[1], "y is reported as shared");
    check(agree.position[2], "z is reported as shared");
    check(!agree.allAgree(), "the selection does not agree overall");

    const std::vector<const Entity*> one = {&a};
    check(multiedit::agreementOf(one).allAgree(),
          "a selection of one agrees with itself");

    const std::vector<const Entity*> none;
    check(multiedit::agreementOf(none).allAgree(),
          "an empty selection agrees vacuously");
}

int main()
{
    testOneAxisTravelsAlone();
    testNoOpDoesNotTravel();
    testMaterialNeedsAPrefab();
    testLightFieldsFanOutExceptPhase();
    testAgreement();

    if (gFailures != 0) {
        std::cerr << "EditorMultiEditTests: " << gFailures << " failure(s)\n";
        return 1;
    }
    std::cout << "EditorMultiEditTests: ok\n";
    return 0;
}
