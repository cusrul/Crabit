#pragma once

// Storage backend selection belongs to the consuming build, not a board SDK.
// Dynamic profile preserves standard heap, strings and unbounded collections.
// Pool profile preserves the original application's injected pool and limits.
#ifndef NANOUI_DYNAMIC_STORAGE
#define NANOUI_DYNAMIC_STORAGE 1
#endif
