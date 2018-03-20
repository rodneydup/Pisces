# PISCES

Pisces Interactive Spectral Compression Engine & Synthesizer

PISCES is an instrument for exploring spectral compression. Spectral Compression is a paradigm for additive synthesis and a audio processing technique.

- - -

Status: Buggy and Crashy, but more or less working! 

To-Do:
- Fix intermittent white vertical bar on spectrograph
- Fine-tune scaling
- Add Y axis labels on spectrograph
- Add Load File and Save Settings functions
- Add Record to WAV Function
- Figure out why Log scale compression is crashing. (changed upper edge case to 20000 as a workaround for  now)
- zero-padding?

- - -

## How to make it work

This software is based on Karl Yerkes' AudioPlatform. So here are notes on building from his repo:

Cloning:

Media (e.g., .wav and .png files) are held using [Git Large File Storage](https://git-lfs.github.com), so you'll need to install `git-lfs` on your system.

- Linux

  On apt-based systems, you'll need `libglfw3-dev` and `libasound2-dev`. The `build-essential` package will install everything you need to compile and link.

- macOS

  First, install Xcode with `xcode-select --install` on the terminal. Then, install [Homebrew](https://brew.sh). Finally, install the _glfw_ library with `brew install glfw`.

- Windows (TDB)

  TDB: This will probably use MinGW, but maybe `choco`, `vcbuildtools`, and `vcpkg`.


### Building examples

To build and run PISCES, from the main directory use the `./run` script. So: `./run Pisces/Pisces.cpp`
