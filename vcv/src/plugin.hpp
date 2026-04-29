// ============================================================================
// VoidLinkAudio - VoidLinkAudio plugin shared header (VCV Rack 2 plugin)
//
// Part of the VoidLinkAudio R&D project by Julien Bayle / Structure Void.
// https://julienbayle.net    https://structure-void.com
//
// Released under the MIT License - see LICENSE file at repo root.
// Built on top of Ableton Link Audio (see ACKNOWLEDGEMENTS.md).
// Provided AS IS, without warranty of any kind.
// ============================================================================

#pragma once

#include <rack.hpp>

using namespace rack;

// The plugin instance, set in init() in plugin.cpp.
extern Plugin* pluginInstance;

// One Model* per module.
extern Model* modelVoidLinkAudioSend;
extern Model* modelVoidLinkAudioReceive;
