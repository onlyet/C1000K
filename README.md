# C1000K
## 目的
测试单线程reactor能否支持百万连接。
##### 1个连接由5元组唯一确定：
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
添加：

    net.ipv4.tcp_mem = 524288 1048576 1572864  
    net.ipv4.tcp_wmem = 512 512 1024  
    net.ipv4.tcp_rmem = 512 512 1024  
    fs.file-max = 2000000  
    net.nf_conntrack_max = 2000000   
    fs.nr_open=2000000  

```
sudo modprobe ip_conntrack  
sudo sysctl –p  
```
  
### client sysctl设置  
    sudo vim /etc/sysctl.conf  
 
 添加：

    net.ipv4.tcp_mem = 262144 524288 1048576  
    net.ipv4.tcp_wmem = 256 256 512  
    net.ipv4.tcp_rmem = 256 256 512  
    fs.file-max = 2000000  
    net.nf_conntrack_max = 2000000    
    fs.nr_open=2000000  
  
```
sudo modprobe ip_conntrack  
sudo sysctl –p  
```
##### 参数解释
* tcp_mem的单位是页，默认4KB  
* 上面将server的内核TCP内存页最大值设为1572864，即6G内存，client是1048576，即4G内存  
* tcp_wmem和tcp_rmem的单位是字节，降低该值，较少内存占用，server设置最低值512，client设置最低值256。测试完应该注释掉，避免缓冲区太小导致ssh连不上等问题。  
* 当打开文件超过1048576时要先设置fs.nr_open，否则下面limits.conf设置无效

## 修改进程最大fd数量（ulimit）  

    sudo vim /etc/security/limits.conf  
    
最后加上4行：  
  
    *               soft    core            unlimited  
    *               soft    stack           12000  
    *               soft    nofile          2000000  
    *               hard    nofile          2000000  
    
##### 参数解释
* 几个值是2000000的参数都是保证理论最大连接数能达到200万  
* stack 设置栈的最大容量，单位是KB。200万连接分摊给2个client，1个client是100万，epoll_event结构体占用12字节，12x100万=1200万=12000KB，所以设置stack为12000

## 编译
make

## 运行server
./server

## 运行client
./client server_ip server_first_port  

* server_first_port server开启100个端口，默认端口范围是9900-9999，故server_first_port是9900  

## 测试结果
下图191.7是server，191.20和191.21是client

达到100万连接，server占用4.86G内存，client占用1.4G内存。减去初始值server占用了2.6G左右，client占用了1.2G左右。
![avatar](/screenshot/1.png)

115万连接的时候server被kill
![avatar](/screenshot/2.png)

恢复正常后的内存占用，可以看到server占用2.1G，client占用200M左右
![avatar](/screenshot/3.png)

server被kill后/var/log/syslog的日志输出，out of memory
![avatar](/screenshot/4.png)
![avatar](/screenshot/5.png)
