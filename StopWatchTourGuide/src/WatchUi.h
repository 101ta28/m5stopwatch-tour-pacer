#pragma once
#include "App.h"

namespace lgfx { inline namespace v1 { class LGFXBase; struct Panel_Device; } }
namespace tour {
void configureWatchTouch(lgfx::Panel_Device& panel);
void drawWatchView(lgfx::LGFXBase& canvas, const View& view);
}
