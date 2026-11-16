// Musical scales, same list and order as norns' musicutil (used by less concepts).
#pragma once

namespace gridworks {

struct ScaleDef {
	const char* name;
	int n;            // notes per octave
	int iv[12];       // semitones above the root
};

static const ScaleDef SCALES[] = {
	{"Major", 7, {0, 2, 4, 5, 7, 9, 11}},
	{"Natural Minor", 7, {0, 2, 3, 5, 7, 8, 10}},
	{"Harmonic Minor", 7, {0, 2, 3, 5, 7, 8, 11}},
	{"Melodic Minor", 7, {0, 2, 3, 5, 7, 9, 11}},
	{"Dorian", 7, {0, 2, 3, 5, 7, 9, 10}},
	{"Phrygian", 7, {0, 1, 3, 5, 7, 8, 10}},
	{"Lydian", 7, {0, 2, 4, 6, 7, 9, 11}},
	{"Mixolydian", 7, {0, 2, 4, 5, 7, 9, 10}},
	{"Locrian", 7, {0, 1, 3, 5, 6, 8, 10}},
	{"Whole Tone", 6, {0, 2, 4, 6, 8, 10}},
	{"Major Pentatonic", 5, {0, 2, 4, 7, 9}},
	{"Minor Pentatonic", 5, {0, 3, 5, 7, 10}},
	{"Major Bebop", 8, {0, 2, 4, 5, 7, 8, 9, 11}},
	{"Altered Scale", 7, {0, 1, 3, 4, 6, 8, 10}},
	{"Dorian Bebop", 8, {0, 2, 3, 4, 5, 7, 9, 10}},
	{"Mixolydian Bebop", 8, {0, 2, 4, 5, 7, 9, 10, 11}},
	{"Blues Scale", 6, {0, 3, 5, 6, 7, 10}},
	{"Diminished Whole Half", 8, {0, 2, 3, 5, 6, 8, 9, 11}},
	{"Diminished Half Whole", 8, {0, 1, 3, 4, 6, 7, 9, 10}},
	{"Neapolitan Major", 7, {0, 1, 3, 5, 7, 9, 11}},
	{"Hungarian Major", 7, {0, 3, 4, 6, 7, 9, 10}},
	{"Harmonic Major", 7, {0, 2, 4, 5, 7, 8, 11}},
	{"Hungarian Minor", 7, {0, 2, 3, 6, 7, 8, 11}},
	{"Lydian Minor", 7, {0, 2, 4, 6, 7, 8, 10}},
	{"Neapolitan Minor", 7, {0, 1, 3, 5, 7, 8, 11}},
	{"Major Locrian", 7, {0, 2, 4, 5, 6, 8, 10}},
	{"Leading Whole Tone", 7, {0, 2, 4, 6, 8, 10, 11}},
	{"Six Tone Symmetrical", 7, {0, 1, 4, 5, 8, 9, 11}},
	{"Balinese", 5, {0, 1, 3, 7, 8}},
	{"Persian", 7, {0, 1, 4, 5, 6, 8, 11}},
	{"East Indian Purvi", 7, {0, 1, 4, 6, 7, 8, 11}},
	{"Oriental", 7, {0, 1, 4, 5, 6, 9, 10}},
	{"Double Harmonic", 7, {0, 1, 4, 5, 7, 8, 11}},
	{"Enigmatic", 7, {0, 1, 4, 6, 8, 10, 11}},
	{"Overtone", 7, {0, 2, 4, 6, 7, 9, 10}},
	{"Eight Tone Spanish", 8, {0, 1, 3, 4, 5, 6, 8, 10}},
	{"Prometheus", 6, {0, 2, 4, 6, 9, 10}},
	{"Gagaku Rittsu Sen Pou", 6, {0, 2, 5, 7, 9, 10}},
	{"In Sen Pou", 5, {0, 1, 5, 2, 8}},
	{"Okinawa", 5, {0, 4, 5, 7, 11}},
	{"Chromatic", 12, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}},
};
static const int NUM_SCALES = sizeof(SCALES) / sizeof(SCALES[0]);

// Semitones of scale step k (k = 0 is the root), walking up octave by octave.
inline int scaleNote(int scale, int k) {
	const ScaleDef& s = SCALES[scale];
	return 12 * (k / s.n) + s.iv[k % s.n];
}

} // namespace gridworks
