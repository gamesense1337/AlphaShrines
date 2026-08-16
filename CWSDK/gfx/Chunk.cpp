#include "Chunk.h"

#include <windows.h>
#include <mmsystem.h>

void gfx::Chunk::Remesh() {
    remesh_time = timeGetTime();
    needs_remesh = true;
}
