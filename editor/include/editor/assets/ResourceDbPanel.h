#pragma once

#include <eng/acp/Pipeline.h>
#include <eng/content/ResourceDb.h>

#include <cstdint>
#include <string>
#include <vector>

// The Resource Database Management Tool -- the box in the top-right of figure
// 1.33, feeding the Resource DB that feeds the pipeline.
//
// The book's version at Naughty Dog was a separate C# application called
// Builder. Here it is a tab in the editor's asset browser, for the reason the
// diagram itself suggests: the editor is already the tool that knows what
// assets exist, and a second application would need its own copy of that.
//
// What it is for is the loop the pipeline made possible and nothing else
// exposes: see every asset the engine knows about, see which are conditioned
// and which are stale, change an asset's import settings, and rebuild just that
// one -- without leaving the editor or learning a command line.
//
// Self-contained on purpose. It owns its database, its filter and its build,
// and EditorApp calls draw(). Nothing here reaches into the scene.
namespace ed {

class ResourceDbPanel {
public:
    // Scans the content root and loads the pack manifest. Cheap enough to call
    // on demand (a 633-record tree is ~150 ms, dominated by hashing) and the
    // panel does it lazily on first draw, so opening the editor does not pay
    // for a tab nobody opened.
    void refresh();

    // One frame of the panel. Safe to call before refresh(); it will scan.
    void draw();

    // True while a build launched from this panel is running.
    bool building() const { return mBuilding; }

    // One line of the table. Public because it is what the panel *shows*, and
    // the little formatting helpers that render a state are free functions.
    struct Row {
        const eng::content::Record* record = nullptr;
        // Current, Stale and Unbuilt are the three answers a person wants, and
        // they come from comparing the record's build key against the pack's.
        enum class State : uint8_t { Current, Stale, Unbuilt, Skipped, NoExporter };
        State state = State::Unbuilt;
        std::string output;
        uint64_t outputBytes = 0;
    };

private:
    void rebuildRows();
    void drawToolbar();
    void drawTable();
    void drawDetail();
    void runBuild(const std::string& filter);

    eng::content::ResourceDb mDb;
    eng::content::PackManifest mPack;
    eng::acp::ExporterRegistry mRegistry;
    std::vector<Row> mRows;

    bool mScanned = false;
    bool mBuilding = false;
    char mFilter[128] = {};
    int mTypeFilter = -1; // -1 = every type
    bool mOnlyStale = false;
    int mSelected = -1;

    // The last build's summary line, kept so the panel can say what happened
    // rather than just going quiet.
    std::string mStatus;
    std::vector<std::string> mErrors;

    // Edits to the selected record's settings, held until Save writes the
    // .meta. Editing straight into the database would make a mis-click a file
    // change, and the whole point of the sidecar is that it is reviewable.
    bool mDirty = false;

    // Anything that rescans the database runs at the END of the frame, never
    // from inside the widget that asked for it. Rescanning replaces the record
    // vector, and every Row here holds a pointer into it -- so a button that
    // rebuilt in place left the rest of the frame reading freed memory. It
    // happened to be harmless only because nothing downstream dereferenced it.
    enum class Pending : uint8_t { None, Refresh, Stamp, Build };
    Pending mPending = Pending::None;
    std::string mPendingFilter;
};

} // namespace ed
