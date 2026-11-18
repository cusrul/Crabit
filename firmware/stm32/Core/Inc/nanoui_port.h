#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Runs the NanoUI main loop. Called once from StartDefaultTask.
void NanoUI_MainLoop(void);
// Creates the UI memory adapter before Board initialises services.
void NanoUI_InitAllocator(void);

#ifdef __cplusplus
}
#endif
