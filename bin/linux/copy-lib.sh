#! /bin/bash
#
mkdir lib
 
dependList=$( ldd ./jxqy | awk '{if (match($3,"/")){ print $3}}' )

cp $dependList ./lib
