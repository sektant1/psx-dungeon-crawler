// What a viewport click on a composed object selects.
//
// The rule is invisible until somebody drags: picking a candle and moving it
// pulls one candle out of the chandelier, and nothing says so until the author
// looks at the result. Which is why it is a pure function with a test rather
// than three lines inside the picker.

#include <editor/scene/PickTarget.h>

#include <cstdlib>
#include <iostream>
#include <string>

using namespace ed;
using game::content::AuthorId;
using game::content::Entity;
using game::content::SceneDocument;

static void require(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "EditorPickTargetTests: " << message << '\n';
        std::exit(1);
    }
}

// chandelier -> arm -> candle, plus a loose barrel that is nobody's child.
static SceneDocument makeDocument()
{
    SceneDocument document;
    const auto add = [&document](const char* id, const char* parent) {
        Entity entity;
        entity.id = id;
        entity.parent = parent;
        document.entities.push_back(entity);
    };
    add("chandelier", "");
    add("arm", "chandelier");
    add("candle", "arm");
    add("barrel", "");
    return document;
}

int main()
{
    const SceneDocument document = makeDocument();

    // --- a free entity is its own object ----------------------------------
    require(resolvePickTarget(document, "barrel", {}, false) == "barrel",
            "an unparented entity resolves to itself");

    // --- clicking any part takes the whole object -------------------------
    require(resolvePickTarget(document, "candle", {}, false) == "chandelier",
            "a click on a leaf selects the object it belongs to");
    require(resolvePickTarget(document, "arm", {}, false) == "chandelier",
            "and so does a click on a middle link");
    require(resolvePickTarget(document, "chandelier", {}, false) == "chandelier",
            "clicking the root is already the object");

    // --- Alt means this exact piece ---------------------------------------
    require(resolvePickTarget(document, "candle", {}, true) == "candle",
            "alt-click takes the exact hit, however deep");

    // --- a second click, from inside, drills in ---------------------------
    // The rule that makes adjusting one candle two clicks instead of a modifier
    // nobody remembers.
    require(resolvePickTarget(document, "candle", {"chandelier"}, false) ==
                "candle",
            "with the object selected, a click goes inside it");
    require(resolvePickTarget(document, "arm", {"candle"}, false) == "arm",
            "and stays inside once there, from any part of the object");

    // --- a selection elsewhere does not count as being inside -------------
    require(resolvePickTarget(document, "candle", {"barrel"}, false) ==
                "chandelier",
            "having something else selected still selects the whole object");

    // --- rootOf ------------------------------------------------------------
    require(rootOf(document, "candle") == "chandelier", "walks to the top");
    require(rootOf(document, "barrel") == "barrel", "a root is its own root");
    require(rootOf(document, "missing") == "missing",
            "an unknown id resolves to itself rather than looping");

    // --- a parent cycle must not hang the editor ---------------------------
    // validate() reports the cycle; this only has to terminate, because the
    // editor that would fix it is the one that would hang.
    {
        SceneDocument broken;
        Entity a, b;
        a.id = "a";
        a.parent = "b";
        b.id = "b";
        b.parent = "a";
        broken.entities = {a, b};
        const AuthorId root = rootOf(broken, "a");
        require(root == "a" || root == "b", "a cycle terminates at something");
        (void)resolvePickTarget(broken, "a", {}, false);
    }

    std::cout << "EditorPickTargetTests: ok\n";
    return 0;
}
