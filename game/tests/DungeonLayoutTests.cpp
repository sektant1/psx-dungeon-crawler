#include "DungeonGen.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {
void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "DungeonLayoutTests: " << message << '\n';
        std::exit(1);
    }
}
}

int main()
{
    const gen::Layout first = gen::generate(7);
    const gen::Layout again = gen::generate(7);
    require(first.valid(), "generated layout must validate");
    require(first.rows() == again.rows(), "generation must be deterministic");
    require(first.spawn().valid() && first.anchor().valid() && first.exit().valid(),
            "generated markers must exist");
    require(first.roomCount() >= 6, "generated layout must contain rooms");
    int props = 0;
    for (const std::string& row : first.rows())
        for (char cell : row)
            if (cell == 'H' || cell == 'B' || cell == 'R' || cell == 'V')
                ++props;
    require(props >= first.roomCount(), "generated rooms should receive dressing");
    for (int i = 0; i < first.archCount(); ++i)
        require(first.arch(i).roomA >= 0, "every arch must join a room");

    const gen::Layout duplicate = gen::Layout::fromRows({
        "#####", "#SSC#", "#####"});
    require(!duplicate.valid(), "duplicate spawn must fail validation");

    const gen::Layout normalized = gen::Layout::fromRows({
        "#####", "#S.##", "#AC##", "#####"});
    require(normalized.valid(), "malformed arch should degrade to floor");
    require(normalized.cellAt(1, 2) == '.', "malformed arch normalization");

    const gen::Layout disconnected = gen::Layout::fromRows({
        "#######", "#S.C###", "#######", "###.###", "#######"});
    require(!disconnected.valid(), "disconnected floor must fail validation");
    return 0;
}
