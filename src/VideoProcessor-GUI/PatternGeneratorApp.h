#pragma once


// Runs the mutually-exclusive native calibration-pattern application. The
// function owns its message loop and returns a process exit code.
int RunPatternGenerator(HINSTANCE instance);
