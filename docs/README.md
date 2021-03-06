# [tinyTerm](http://yongchaofan.github.io/tinyTerm)

[![Build Status](https://travis-ci.org/pages-themes/minimal.svg?branch=master)](https://travis-ci.org/pages-themes/minimal) 

*Minimalist terminal emulator, designed by network engineer for network engineers, with unique features for effeciency and effectiveness when managing network devices like routers, switches, transponders and ROADMs through command line interface.*

![Thumbnail of minimal](tinyTerm-0.png)


## Project philosophy
tinyTerm is intended to be small, simple and scriptable, x64 executable is only 244KB, source code consist of 6 files totaling 150KB, ~6000 sloc, libssh2 is the only external library required to build on Windows 7/10. 

User interface design is minimal, main menu shares title bar space, scrollbar hidden until user trys to scroll back, only one dialog for making connections, 


## Building
Makefiles are provided for building with MSYS2+MingW64/32, also a cmd file for building with Visual Studio building tools.

    Makefile   building 32bit tinyTerm.exe for Windows 7/10 using wincng crypto
    Make.cmd   command for Visual Studio 2019 build tool
    
### Librarys
    libssh2 using release 1.9.0 with pull request #397 for full support of WinCNG crypto functions
            ./configure --with-crypto=wincng
            make install
            
    mbedTLS Since Windows XP doesn't have WinCNG support, external crypto library has to be used, 
            download mbedtls-2.7.9, "make WINDOWS_BUILD=1 no_test install", then build libssh2
            ./configure --with-crypto=mbedtls
            make install
            
### Soruce files
    tiny.h  header file for all function definitions
    tiny.c  winmain and UI functions
    term.c  simple xterm compatible terminal implementation
    host.c  serial and telnet host implementation, plus http/ftp/tftp servers
    ssh2.c  ssh/sftp/netconf host implementation based on libssh2
    auto_drop.c COM wrapper for auto completion and drag&drop function
    

## Contributing
Interested in contributing to tinyTerm? I'd love your help. tinyTerm is made an open source project for users and developers to contribute and make it better together. See [the CONTRIBUTING file](docs/CONTRIBUTING.md) for instructions on how to contribute.


## Roadmap
See the [open issues](https://github.com/zoudaokou/tinyTerm/issues) for a list of proposed features (and known issues).
