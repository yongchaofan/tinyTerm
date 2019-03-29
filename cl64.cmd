rc res\tinyTerm.rc
cl -Ox /DUNICODE /I..\x64\include tiny.c term.c host.c ssh2.c auto_drop.c res\tinyTerm.res user32.lib gdi32.lib comdlg32.lib comctl32.lib ole32.lib shell32.lib ws2_32.lib winmm.lib bcrypt.lib crypt32.lib shlwapi.lib ..\x64\lib\libssh2.lib /MD /link /out:tinyTerm64.exe
