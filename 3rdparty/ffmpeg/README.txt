FFmpeg 4.4.8 static libraries for VideoProcessor
================================================

Version and source
------------------

This directory is built from the unmodified upstream FFmpeg 4.4.8 release:

  Tag:      n4.4.8
  Commit:   209fceb8783e7996d04f35503d62fae0c42f2575
  Source:   https://git.ffmpeg.org/ffmpeg.git

The previous project-specific FFmpeg fork is no longer used. VideoProcessor performs its V210
conversions natively, and the former fork-only R12B decoder is now implemented by the native
CR12BtoRGB48VideoFrameFormatter. Upstream FFmpeg remains in use for R210 decoding/scaling and
FFmpeg logging.

These are static libraries, so users do not install FFmpeg or ship FFmpeg DLLs separately.

License and attribution
-----------------------

The libraries are configured with --enable-gpl. See COPYING.GPLv2, CREDITS, and MAINTAINERS in
this directory, and the FFmpeg source tree for the complete corresponding license information.

Rebuilding the libraries
------------------------

Prerequisites:

* Visual Studio 2019 C++ tools (v142), using an x64 Native Tools command prompt.
* MSYS2 with make, diffutils, yasm, rsync, and pkg-config.
* MSYS2_PATH_TYPE=inherit so configure uses the Visual C++ compiler and linker.
* A clean checkout of official FFmpeg tag n4.4.8 at the commit shown above.

Release build (static runtime-compatible /MD libraries):

  mkdir -p ffmpeg_build_release
  cd ffmpeg_build_release
  ../ffmpeg-4.4.8/configure --toolchain=msvc --disable-shared --enable-static \
    --arch=x86_64 --target-os=win64 --enable-asm --enable-x86asm \
    --disable-avdevice --disable-doc --disable-bzlib --disable-libopenjpeg \
    --disable-iconv --disable-zlib --disable-libopus --disable-encoder=libopus \
    --disable-decoder=libopus --disable-encoder=opus --disable-decoder=opus \
    --disable-mediafoundation --enable-gpl --disable-network \
    --prefix=../ffmpeg_install_release --extra-cflags=-MD --extra-cxxflags=-MD
  make -j6
  make install

Debug build (static runtime-compatible /MDd libraries):

  mkdir -p ffmpeg_build_debug
  cd ffmpeg_build_debug
  ../ffmpeg-4.4.8/configure --toolchain=msvc --disable-shared --enable-static \
    --arch=x86_64 --target-os=win64 --enable-asm --enable-x86asm \
    --disable-avdevice --disable-doc --disable-bzlib --disable-libopenjpeg \
    --disable-iconv --disable-zlib --disable-libopus --disable-encoder=libopus \
    --disable-decoder=libopus --disable-encoder=opus --disable-decoder=opus \
    --disable-mediafoundation --enable-gpl --disable-network --enable-debug \
    --prefix=../ffmpeg_install_debug --extra-cflags=-MDd --extra-cxxflags=-MDd
  make -j6
  make install

Copy the installed include tree to 3rdparty/ffmpeg/include. Copy libavcodec.a, libavutil.a, and
libswscale.a from each install's lib directory into the matching msvc2019_x64_release or
msvc2019_x64_debug directory. Headers and both library configurations must always come from the
same FFmpeg 4.4.8 source revision; mixing FFmpeg ABI versions is unsupported.

Expected SHA-256 hashes for the locally linked libraries:

  Release libavcodec.a  4E418BAA9B083C0B0702F42D7FDC8309A076539AD831A7758E5B83D1E9EFD8DC
  Release libavutil.a   E212C812C182EA872F04A623B4C0C09B6D2185A40BB46FF6374C8F5070E8EA51
  Release libswscale.a  79F3B1D12B5B27B9649F24AD68D7E31AD698EDAD0D00A3E50A702F2250AF7664
  Debug   libavcodec.a  30C3F95000788FD584E6800513497FB57FD97DCF6F1D75B110CBA1F59FBB93CA
  Debug   libavutil.a   12F0CC92E5E54048B80B80B5FC92FF25F8503CF7374E907F15ECC553B00AAB7C
  Debug   libswscale.a  7B553B38C24D753DCF048F7E76E8A4DA0212ED8273E385B923D739CBDCD8D550
