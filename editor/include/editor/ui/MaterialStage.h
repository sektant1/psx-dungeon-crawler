#pragma once
// The material staging rig moved into the engine (eng::MaterialPreview) so the
// game's debug UI and the psx demo can stage a material too -- it is a renderer
// concern, not an editor one. These aliases keep the editor's own vocabulary.
#include <eng/render/MaterialPreview.h>

namespace ed {

using StagePreview = eng::StagePreview;
using StagePreviewCatalog = eng::StagePreviewCatalog;
using MaterialStage = eng::MaterialPreview;

} // namespace ed
