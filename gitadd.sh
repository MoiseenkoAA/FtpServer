#!/bin/sh

git add FtpServer.h FtpServer.cpp Constants.h Constants.cpp version_.h CryptRSAGenKeys.cpp Temp.h stdafx.h stdafx.cpp keys.h
git add gitadd.sh .github
git add FtpServer1.vcxproj FtpServer1.rc Makefile Licence.txt

if [ x"$1" = "x" ]; then
    echo 'git commit -m "Comment"'
    echo 'git push origin main'
else
    git commit -m "$1" ; git push origin main
fi
