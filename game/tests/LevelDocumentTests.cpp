#include "LevelDocument.h"

#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "LevelDocumentTests: " << message << '\n';
        std::exit(1);
    }
}
}

int main()
{
    LevelDocument doc({"#######", "#S.C.X#", "#######"});
    require(doc.validated(true).valid(), "fixture must validate");
    require(doc.paint(2, 1, 'L'), "painting should change a cell");
    require(doc.cell(2, 1) == 'L', "painted cell should hold brush");
    require(doc.undo() && doc.cell(2, 1) == '.', "undo restores previous cell");
    require(doc.redo() && doc.cell(2, 1) == 'L', "redo restores edit");

    require(doc.paint(2, 1, 'H'), "chest marker should be editable");
    require(doc.validated(true).valid(), "prop markers remain walkable and valid");

    require(doc.paint(4, 1, 'S'), "unique marker can be moved");
    require(doc.cell(1, 1) == '.', "old unique marker becomes floor");
    require(doc.cell(4, 1) == 'S', "new unique marker is placed");

    doc.replace(gen::generate(42).rows());
    require(doc.validated(true).valid(), "generated import remains valid");
    require(doc.undo(), "replace participates in undo history");
    require(doc.cell(4, 1) == 'S', "undo restores document before import");
    return 0;
}
