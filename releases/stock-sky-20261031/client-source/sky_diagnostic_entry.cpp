// Explicit test entry in the SAME normal client executable. No production
// startup, live loop, scene, physics, or rendering path invokes it implicitly.
#define wmain hlc_sky_diagnostic_entry
#include "tests/sky_candidate_fixture.cpp"
#undef wmain
