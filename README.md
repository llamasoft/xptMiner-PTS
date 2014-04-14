xptMiner-PTS, an OpenCL PTS miner
=================================

xptMiner is a multi-algorithm miner and reference implementation of the xpt protocol.
xptMiner-PTS is a Protoshare only implimentation of the xptMiner program.

xptMiner is by jh00, GPU code by Gigawatt, OpenCL libraries by Girino

This has a 3% donation which can be set using the -f option (-f 5.0 would be 5.0% donation)


Prerequisites
=============

**CentOS:**
```
sudo yum groupinstall "Development Tools"
sudo yum install openssl openssl-devel openssh-clients gmp gmp-devel gmp-static git
```

**Ubuntu:**
```
sudo apt-get -y install build-essential m4 openssl libssl-dev git libjson0 libjson0-dev libcurl4-openssl-dev 
```


Building
========
```
git clone https://github.com/llamasoft/xptMiner-PTS.git
cd xptMiner-PTS
make
```