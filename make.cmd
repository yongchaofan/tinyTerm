rc res\tinyTerm.rc

cl /c -O1 /GL /MT /DUNICODE /I../%Platform%/include %1 %2 %3 %4 %5 %6 %7 %8 %9

link /LTCG /NXCOMPAT /DYNAMICBASE /NODEFAULTLIB:libucrt.lib ucrt.lib tiny.obj term.obj host.obj ssh2.obj auto_drop.obj res\tinyTerm.res user32.lib gdi32.lib comdlg32.lib comctl32.lib ole32.lib shell32.lib ws2_32.lib winmm.lib ntdll.lib bcrypt.lib crypt32.lib shlwapi.lib Advapi32.lib ../%Platform%/lib/libssh2.lib /out:tinyTerm_%Platform%.exe
