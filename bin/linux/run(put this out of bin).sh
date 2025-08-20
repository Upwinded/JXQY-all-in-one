#! /bin/bash
# goto the directory
cd bin/linux/
# set
chmod 777 JXQY*

# set lib path
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/lib:/usr/lib64:/lib:/lib64:/usr/local/lib:/usr/local/lib/x86_64-linux-gnu:/lib/x86_64-linux-gnu:/usr/lib/x86_64-linux-gnu:${PWD}/lib

# run jxqy
exec ./JXQY*
