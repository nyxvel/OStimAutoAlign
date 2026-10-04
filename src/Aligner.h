#pragma once

// Reacts to OStim scene changes: measures the scene's main action for one animation loop, solves
// the correction and hands it to OStim's own alignment (so OStim keeps and reapplies it, and the
// align menu still works). Keeps a record of what it wrote in My Games\...\OStim\autoalign.json, so
// it can tell its own values from ones set by hand.
namespace Aligner
{
	void Init();
}
