#!/bin/bash

#version rule:MAJORVERSION.MINORVERSION.COMMIT_COUNT-g(COMMIT_ID)

OUT_DIR=
if [ $# == 1 ];then
    OUT_DIR=$1/
    mkdir -p "${OUT_DIR}src"
fi

BASE=$(pwd)
echo $BASE

#major version
MAJORVERSION=1

#current minor version,if release a new version,please update it
MINORVERSION=3

#last release version commit id,please store commit id here every release version
RELEASE_COMMIT_ID=bcd880e8

#modue name/
MODULE_NAME=MM-module-name:gst-plugin-video-sink

#get all commit count from last release version
COMMIT_COUNT=$(git rev-list $RELEASE_COMMIT_ID..HEAD --count)
echo commit count $COMMIT_COUNT

#get current commit id
COMMIT_ID=$(git rev-parse --short HEAD)
echo commit id $COMMIT_ID

cp "src/aml_version.h.in" "${OUT_DIR}src/aml_version.h.tmp"
#find the module name line
MODULE_NAME_LINE=`sed -n '/\"MM-module-name/=' ${OUT_DIR}src/aml_version.h.tmp`
#echo $VERSION_LINE

#version rule string
VERSION_STRING=${MAJORVERSION}.${MINORVERSION}.${COMMIT_COUNT}-g${COMMIT_ID}
echo version: $VERSION_STRING

#update the original version
if [ ${MODULE_NAME_LINE} -gt 0 ]; then
sed -i -e ${MODULE_NAME_LINE}s"/.*/\"${MODULE_NAME},version:${VERSION_STRING}\"\;/" ${OUT_DIR}src/aml_version.h.tmp
fi

#if version.h already exist, compare the content, if it's the same, using the original file, avoid rebuild
if [ -e "${OUT_DIR}src/aml_version.h" ]; then
	file1_hash=$(md5sum ${OUT_DIR}src/aml_version.h | awk '{print $1}')
	file2_hash=$(md5sum ${OUT_DIR}src/aml_version.h.tmp | awk '{print $1}')

	if [ "$file1_hash" == "$file2_hash" ]; then
		echo "version file content is the same"
		rm ${OUT_DIR}src/aml_version.h.tmp
	else
		echo "version file content is not the same, use the tmp file"
		mv ${OUT_DIR}src/aml_version.h.tmp ${OUT_DIR}src/aml_version.h
	fi
else
	echo "no version file, use the tmp version file"
	mv ${OUT_DIR}src/aml_version.h.tmp ${OUT_DIR}src/aml_version.h
fi
