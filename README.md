![VideoProcessor banner](https://github.com/defl/videoprocessor/blob/main/images/vp%20banner.png)

:film_projector: VideoProcessor turns a computer into a 4k HDR capable live video processor by connecting a video capture card to a renderer and taking care of details such as conversion, timing and HDR metadata.

This allows advanced renderers to do things like 3D LUT, HDR tone mapping, scaling, deinterlacing and much more which can significantly improve image quality on most displays and beamers. 

_Capture cards cannot capture HDCP protected data, VideoProcessor can only process what can be captured._

# Install or update a release ZIP

Extract the complete ZIP, then run **SETUP-RUNTIME.cmd** before opening
VideoProcessor or `config/VideoProcessorConfig.exe`. Setup checks the Microsoft
Visual C++ **x64** runtime version and installs the included official package
only when needed. Approve its Windows administrator prompt; if a restart is
required, restart and rerun setup. See the ZIP's `START-HERE.txt`.

Preserve your existing `VideoProcessor.cfg`, `VideoProcessor.state`, and personal
assets when updating. `VideoProcessor.cfg.example` is a reference, not a
replacement for your settings. Keep the main executable and renderer DLL from
the same release. Config Apply/OK requires the runtime declared in the package;
an older installed Visual C++ runtime can cause a crash even when VP runs.

# Website

You can find all the static details on [videoprocessor.org](http://videoprocessor.org)
- [Getting started](http://videoprocessor.org/getting_started)
- [Manual](http://videoprocessor.org/manual)
- [FAQ](http://videoprocessor.org/faq)
- [Wiki](https://github.com/defl/videoprocessor/wiki)

# Screenshot

![Screenshot](https://github.com/defl/videoprocessor_website/blob/main/site/static/images/screenshot.png)

# License & legal

This application is released under the GNU GPL 3.0 for non-commercial usage, commercial usage to build and sell video processor systems is not allowed, see LICENSE.txt. 

Parts of this code are made and owned by others, for example SDKs; in all such cases there are LICENSE.txt and README.txt files present to point to sources, attributions and licenses.

------

 Copyright 2021 [Dennis Fleurbaaij](mailto:mail@dennisfleurbaaij.com)
