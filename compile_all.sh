#!/bin/bash


PRJT_RT="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

RND=$$
CLUSTER_COMPILE_DIR="md2:~/plumed2_$RND"

echo "Changing Makefile.user"
cd $PRJT_RT

echo "Coping everything on the cluster"

rsync -uha $PRJT_RT/ $CLUSTER_COMPILE_DIR

exit

set +e pipefile
cat <<EOF | ssh md2
#!/bin/bash

set -o nounset
set -e pipefail

echo "COMPILING ON LCMDLC2"

cd ${CLUSTER_COMPILE_DIR/md2:/}/
echo "MAKE CLEAN"
make distclean
./configure --prefix=lcmdlc2
echo "MAKE -j 16"
make -j 16 > log_lcmdlc2

EOF
set -e pipefile

echo "Coping back everything from the cluster"
rsync -uha $CLUSTER_COMPILE_DIR/ $PRJT_RT


echo "COMPILING HERE"
cd $PRJT_RT
make distclean
./configure --prefix=$PWD/local
make -j 16
make

echo "SYNCRONIZE"
clusters_sync

echo "REMOVING TEMP DIRECTORY FROM LCMDLC2"
ssh md2 "rm -rf ${CLUSTER_COMPILE_DIR/md2:/}"


