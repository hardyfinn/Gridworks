#include "plugin.hpp"

Plugin* pluginInstance;

void init(Plugin* p) {
	pluginInstance = p;
	p->addModel(modelGridDrums);
	p->addModel(modelLessConcepts);
	// more Gridworks modules get added here
}
