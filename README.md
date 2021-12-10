# C1000K
## 目的
测试单线程reactor能支否支持百万并发连接。
### 已知1个连接由5元组决定：
* local_ip
* local_port
* peer_ip
* peer_port
* protocal  

已知ubuntu-1604的默认的端口范围：32768-60999  
假设client有20000个fd能用，server开100个端口，用2个client去连接server，则理论最大连接数是：20000 x 100 x 2 = 400万。  

## 代码修改
在reactor的基础上，server开启100个监听端口。  

## 虚拟机配置
PC硬件：I5-9400 6核，内存16G  
准备3个虚拟机（ubuntu-1604），1个当server，2个当client。  
server配置：内存6G，CPU 2处理器1核心  
client配置：内存3G，CPU 1处理器2核心  

## 内存要求
为了避免内存不够导致程序被killed，一定要保证内存充足。  
以下只是我经过测试的估计最大值，实际上不需要这么多内存。  
server可用内存4G以上  
client可用内存3G以上  
这里client我选了个较干净的虚拟机，初始只占用200M内存，根据你的实际情况克隆N份，开越多的client跑满1000k连接越快。我的server初始就占用2.3G，故分配了6G给它。  

## 修改内核TCP参数

### server sysctl设置  
sudo vim /etc/sysctl.conf  
net.ipv4.tcp_mem = 524288 1048576 1572864  
net.ipv4.tcp_wmem = 512 512 1024  
net.ipv4.tcp_rmem = 512 512 1024  
fs.file-max = 2000000  
net.nf_conntrack_max = 2000000   
fs.nr_open=2000000  
  
sysctl –p  
  
如果报错：Error: /proc/sys/net/nf_conntrack_max no such file or directory  
解决方法：sudo modprobe ip_conntrack  

### client sysctl设置  
sudo vim /etc/sysctl.conf  
net.ipv4.tcp_mem = 262144 524288 1048576  
net.ipv4.tcp_wmem = 256 256 512  
net.ipv4.tcp_rmem = 256 256 512  
fs.file-max = 2000000  
net.nf_conntrack_max = 2000000    
fs.nr_open=2000000  
  
sysctl –p  


## 修改进程最大fd数量（ulimit）  
/etc/security/limits.conf  
最后加上两行：  
`*               soft    nofile          2000000  
*               hard    nofile          2000000  `
