cp mininet.patch ~/
cd ~/
apt-get update
apt-get -y install linux-tools-5.15.0-1066-gcp linux-tools-gcp
apt-get -y install python-is-python3
git clone https://github.com/mininet/mininet
cd mininet
git checkout -b mininet-2.3.0 2.3.0
mv ../mininet.patch .
git apply mininet.patch
cd ..
mininet/util/install.sh -a
pip install termcolor
pip install networkx
pip install numpy
apt-get -y install bridge-utils


// required c++ liberaries
sudo apt-get -y install nlohmann-json3-dev
sudo apt-get -y install g++-multilib
sudo apt -y install libgcc-10-dev
sudo ln -s /usr/lib/gcc/x86_64-linux-gnu/10/libtsan_preinit.o /usr/lib/libtsan_preinit.o
sudo apt-get install libssl-dev

cd -
cd floodclone
make clean
make 
cd ..


