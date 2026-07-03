#!/bin/bash

cd shellcodecleaner
make
cd ..

CNTFRQ_EL0=$(./shellcodecleaner/main ./CNTFRQ_EL0/Reference/ShellCode.c)

cp ./ShellCodeTemplate.h ../ShellCode.h

sed -i "s/AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA/${CNTFRQ_EL0}/g" ../ShellCode.h
