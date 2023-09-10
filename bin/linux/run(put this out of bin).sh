#! /bin/bash
# goto the directory
cd bin/linux/
# set
chmod 777 jxqy

# set lib path
export LD_LIBRARY_PATH=${LD_LIBRARY_PATH}:${PWD}/lib

# run jxqy
exec ./jxqy
