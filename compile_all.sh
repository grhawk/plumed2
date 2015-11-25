#!/bin/bash


PRJT_RT="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

RND=$$
CLUSTER_COMPILE_DIR="md2:~/plumed2_$RND"

cd $PRJT_RT

echo "MAKE CLEAN"
make distclean > log_clean
make fullclean >> log_clean

echo ''
echo 'All non-tracked file will be removed by git clean'
echo 'Do you want to run git clean? [y/N]'
read answer
if [[ "$answer" == 'y' ]]; then
  git clean -fd
  git clean -fX
  git clean -f
  git clean -fdX
fi

echo "Coping everything on the cluster"
rsync -uha $PRJT_RT/ $CLUSTER_COMPILE_DIR

set +e pipefile
cat <<EOF | ssh md2
#!/bin/bash

set -o nounset
set -e pipefail

echo "COMPILING ON LCMDLC2"

module load intelmpi/5.1.1

cd ${CLUSTER_COMPILE_DIR/md2:/}/
./configure --prefix=\$PWD/lcmdlc2 &> log_lcmdlc2
echo "MAKE -j 16"
make -j 16 &>> log_lcmdlc2
make install &>> log_lcmdlc2

EOF
set -e pipefile

echo "Coping back everything from the cluster"
rsync -uha $CLUSTER_COMPILE_DIR/ $PRJT_RT

echo "COMPILING HERE"
cd $PRJT_RT
mv lcmdlc2 ../tmp-$RND
if [[ "$answer" == 'y' ]]; then
  git clean -fd
  git clean -fX
  git clean -f
  git clean -fdX
fi
./configure --prefix=$PWD/local &> log_local
make -j 16 &>> log_local
make install &>> log_local
mv ../tmp-$RND lcmdlc2

echo "SYNCRONIZE"
clusters_sync

echo "REMOVING TEMP DIRECTORY FROM LCMDLC2"
ssh md2 "rm -rf ${CLUSTER_COMPILE_DIR/md2:/}"

