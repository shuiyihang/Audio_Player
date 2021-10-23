### libmad介绍
INTRODUCTION

  MAD (libmad) is a high-quality MPEG audio decoder. It currently supports
  MPEG-1 and the MPEG-2 extension to Lower Sampling Frequencies, as well as
  the so-called MPEG 2.5 format. All three audio layers (Layer I, Layer II,
  and Layer III a.k.a. MP3) are fully implemented.

  MAD does not yet support MPEG-2 multichannel audio (although it should be
  backward compatible with such streams) nor does it currently support AAC.

  MAD has the following special features:

    - 24-bit PCM output
    - 100% fixed-point (integer) computation
    - completely new implementation based on the ISO/IEC standards
    - distributed under the terms of the GNU General Public License (GPL)

  Because MAD provides full 24-bit PCM output, applications using MAD are
  able to produce high quality audio. Even when the output device supports
  only 16-bit PCM, applications can use the extra resolution to increase the
  audible dynamic range through the use of dithering or noise shaping.

  Because MAD uses integer computation rather than floating point, it is
  well suited for architectures without a floating point unit. All
  calculations are performed with a 32-bit fixed-point integer
  representation.

  Because MAD is a new implementation of the ISO/IEC standards, it is
  unencumbered by the errors of other implementations. MAD is NOT a
  derivation of the ISO reference source or any other code. Considerable
  effort has been expended to ensure a correct implementation, even in cases
  where the standards are ambiguous or misleading.

  Because MAD is distributed under the terms of the GPL, its redistribution
  is not generally restricted, so long as the terms of the GPL are followed.
  This means MAD can be incorporated into other software as long as that
  software is also distributed under the GPL. (Should this be undesirable,
  alternate arrangements may be possible by contacting Underbit.)


### madlld使用


