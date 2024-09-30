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
pip install numpy
apt-get -y install bridge-utils
