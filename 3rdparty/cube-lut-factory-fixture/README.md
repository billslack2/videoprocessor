# cube-lut-factory.js fixture provenance

`LibplaceboLutParserTests.cpp` embeds the unmodified nonlinear 4x4x4 Cube
example published in the `cube-lut-factory.js` README.

- Repository: https://github.com/diegoinacio/cube-lut-factory.js
- Source commit: `fde633ad057e514bd3f04049cee3289af93cef2b`
- README Git blob: `0ed20e76853c4da321785813b5a608ccc16127ab`
- License: MIT; see `LICENSE.txt` in this directory.

The published generator functions are:

- `R = (r^2 + g*b)/2`
- `G = (g^3 + r*2*b)/3`
- `B = (b^4 + r + b)/3`

This is an interoperability/application fixture with independently calculable
sample values. It is not a display-calibration profile.
