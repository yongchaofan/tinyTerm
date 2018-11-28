# [tinyTerm](http://zoudaokou.github.io/tinyTerm)

[![Build Status](https://travis-ci.org/pages-themes/minimal.svg?branch=master)](https://travis-ci.org/pages-themes/minimal) 


*Minimalist terminal emulator, designed by network engineer for network engineers, with unique features for effeciency and effectiveness when managing network devices like routers, switches, transponders and ROADMs through command line interface.*

![Thumbnail of minimal](thumbnail.png)

## Features

    1. serial/telnet/ssh/sftp/netconf connections
    2. embeded ftpd and tftpd for sile transfer
    3. command history and command autocompletion
    4. text file based batch command automation
    5. xmlhttp interface for easy extension


## Project philosophy

tinyTerm is intended to be small, simple and scriptable, WIN32 executable is only 345KB, source code is consist of 7 .c/.h files totaling 140KB, only required external dependency is libssh2. 

User interface design is minimal too, there is one window, main menu shares title bar space, scrollbar hidden until user trys to scroll back, only one dialog for makeing connections, 


## Building

Makefiles are provided for building with MSYS2/MingW64/MingW32

    Makefile    32bit executable for Windows 7 or above
    Makefile64  64bit executable for Windows 7 or above
    MakefileXP  32bit executable for Windows XP using mbedtls crypto library

### Librarys
    libssh2 using any daily snapshot after March, 2018 for full support of WinCNG crypto functions
            ./configure --with-crypto=wincng --disable-shared
            make install
            
            Since Windows XP doesn't have WinCNG support, external crypto library has to be used, 
            download mbedtls build with default settings and install, then build libssh2
            ./configure --with-crypto=mbedtls --disable-shared
            make install
            
### Soruce files
    tiny.h  header file for all function definitions
    tiny.c  winmain and UI functions
    term.c  simple xterm compatible terminal implementation
    host.c  serial and telnet host implementation
    ssh2.c  ssh/sftp/netconf host implementation based on libssh2
    ftpd.c  ftp and tftp server implementation
    auto_drop.c COM wrapper for auto completion and drag&drop function
    

## Roadmap

See the [open issues](https://github.com/zoudaokou/tinyTerm/issues) for a list of proposed features (and known issues).

## Contributing

Interested in contributing to tinyTerm? I'd love your help. tinyTerm is made an open source project for users and developers to contribute and make it better together. See [the CONTRIBUTING file](docs/CONTRIBUTING.md) for instructions on how to contribute.
