libplacebo 7.360.1
==================

This directory contains the headers and Windows x64 runtime used to build the
optional experimental VideoProcessorLibplacebo.dll renderer plugin. The normal
VideoProcessor executable does not import libplacebo.

Upstream project: https://github.com/haasn/libplacebo
Version: 7.360.1
License: LGPL-2.1-or-later (see LICENSE.txt)

The runtime binaries were obtained from the MSYS2 mingw64 packages. The
libplacebo DLL is dynamically linked so it remains independently replaceable.
The accompanying DLLs are runtime dependencies of that binary.
Their license texts are retained under the licenses directory.

The MSVC import library was generated from the exported symbols of
libplacebo-360.dll; it contains no libplacebo implementation code. Built plugin
packs are staged under x64\<configuration>\libplacebo so the normal executable
can be distributed independently.
