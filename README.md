# lfm

Terminal file manager heavily inspired by [LF](https://github.com/gokcehan/lf), written in C and using [notcurses](https://github.com/dankamongmen/notcurses) and luajit. 
If you like LF but want more flexibility in the configuration and extensibility, this might be for you.

**Note: This is WIP. Nothing in the API is to be considered stable. Use at your own risk.**

![lfm](https://user-images.githubusercontent.com/5224719/185700093-b7df9d8f-3a7f-4382-be88-b1072e8e08c7.png)

## Installation

#### Arch Linux
Use the `PKGBUILD` provided in `pkg`. Builds the latest `master`. Also works on Arch Linux ARM 64bit.

#### Debian/Ubuntu
Install dependencies

    sudo apt install cmake lua-posix libpcre3-dev libmagic-dev luajit libluajit-5.1-dev libreadline-dev zlib1g-dev libunistring-dev libev-dev gcc g++ pkg-config libavformat-dev libswscale-dev libavcodec-dev libdeflate-dev

   
In the root of this repository perform

    mkdir build && cd build
    cmake .. -DCMAKE_INSTALL_PREFIX=~/.local -DCMAKE_BUILD_TYPE=RelWithDebInfo
    make
    make install

This will install `lfm` into `~/.local`, make sure to add `~/.local/bin` to `PATH`. 
To install `lfm` globally (which I don't recommend, yet), replace the `CMAKE_INSTALL_PREFIX` with e.g. `/usr` and run `sudo make install` instead.

    
## Usage
TODO.
