#pragma once

#include "../pipe_client.h"

namespace haloap {

    bool InstallChapterTitleHook(PipeClient* pipe);
    void UninstallChapterTitleHook();

}  // namespace haloap