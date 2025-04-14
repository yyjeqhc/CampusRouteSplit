### 背景：

#### 	在高校校园网IPV4流量收费，IPV6流量免费且流量卡盛行的情况下，合理配置网络资源，降低网费就变得很重要了。

### 目的：

#### 	在PC有线连接校园网，无线连接手机热点的情况下，修改路由表，实现IPV4流量走无线，IPV6走有线。但是，IPV4全走无线就不能访问校园内网了，所以还要针对校园网的网段进行额外配置。

### 编译：

#### 开发环境：https://www.msys2.org/使用msys的编译器，好像自带windows的一些头文件，不行的话还需要额外安装VS的东西。

#### 保留了很多注释，也可以自己耐心的调试运行一下

#### 注：随意修改路由表可能会导致网络活动失败，所以，提前准备了重置一切：

```shell
netsh interface ipv4 reset
netsh interface ipv6 reset
#它会提示要重启，如果网络恢复正常了，就不管它，不然就重启试试
```

```shell
g++ NetConfig.cpp -o NetConfig.exe -liphlpapi -lws2_32 -lwininet

#cl的编译命令如下，当然，此代码会编译失败，需要修改
cl NetConfig.cpp /link iphlpapi.lib ws2_32.lib wininet.lib
```

然后运行即可。

### route命令介绍：

```shell
#打印完整的路由表V4和V6
route print
#打印v4
route print -4
#打印v6
route print -6
#查看网络信息
ipconfig
#查看网卡信息（网络适配器）
PS C:\Users\yyjeqhc> netsh interface ipv4 show interfaces

Idx     Met         MTU          状态                名称
---  ----------  ----------  ------------  ---------------------------
  1          75  4294967295  connected     Loopback Pseudo-Interface 1
 52           5        1280  connected     Tailscale
 19          10        1500  connected     WLAN
  5          25        1500  connected     以太网
 17          25        1500  disconnected  WLAN 3
 10          25        1500  disconnected  WLAN 4
 12          25        1500  disconnected  WLAN 5
  6          35        2800  connected     ZeroTier One [6ab565387a4a8d89]
  7          35        2800  connected     ZeroTier One [48d6023c46f3f5e2]
  4          35        1500  connected     VMware Network Adapter VMnet1
 24          35        1500  connected     VMware Network Adapter VMnet8
```

### 命令行设置V4的默认路由权重

```shell
#直接命令行修改v4路由表的一项默认路由
PS C:\Users\yyjeqhc\Desktop\add\NetworkRouteManager> route print -4
===========================================================================
接口列表
 52...........................Tailscale Tunnel
 19...e8 65 38 99 c8 dd ......Qualcomm FastConnect 7800 Wi-Fi 7 High Band Simultaneous (HBS) Network Adapter
  5...d8 43 ae d5 d9 1c ......Realtek Gaming 2.5GbE Family Controller
 17...2a 65 38 99 c8 dd ......Qualcomm FastConnect 7800 Wi-Fi 7 High Band Simultaneous (HBS) Network Adapter #3
 10...0a 65 38 99 c8 dd ......Qualcomm FastConnect 7800 Wi-Fi 7 High Band Simultaneous (HBS) Network Adapter #4
 12...1a 65 38 99 c8 dd ......Qualcomm FastConnect 7800 Wi-Fi 7 High Band Simultaneous (HBS) Network Adapter #5
  6...8a 78 44 f5 ce 5c ......ZeroTier Virtual Port
  7...e2 00 fd c9 ca 3b ......ZeroTier Virtual Port #2
  4...00 50 56 c0 00 01 ......VMware Virtual Ethernet Adapter for VMnet1
 24...00 50 56 c0 00 08 ......VMware Virtual Ethernet Adapter for VMnet8
  1...........................Software Loopback Interface 1
===========================================================================

IPv4 路由表
===========================================================================
活动路由:
网络目标        网络掩码          网关       接口   跃点数
          0.0.0.0          0.0.0.0   118.202.10.254   118.202.10.157     50
          0.0.0.0          0.0.0.0   25.255.255.254    172.18.66.113  10034
          0.0.0.0          0.0.0.0   25.255.255.254   192.168.192.66  10034
          0.0.0.0          0.0.0.0   192.168.126.96  192.168.126.195     20

PS C:\Users\yyjeqhc\Desktop\add\NetworkRouteManager> netsh interface ipv4 set interface 19 metric=20
确定。

#再route print -4查看，它产生了变化，可能有个基准值吧，我设置20，显示30，设置10，显示20
          0.0.0.0          0.0.0.0   192.168.126.96  192.168.126.195     30
```
### 命令行设置V6的默认路由权重
```shell
#直接命令行修改ipv6的默认路由
route print -6
IPv6 路由表
===========================================================================
活动路由:
 接口跃点数网络目标                网关
  5    281 ::/0                     fe80::96a6:d8ff:fec7:c201
 19     61 ::/0                     fe80::34ff:d7ff:fe5f:d23a
 
#命令和v4的差不多，我把两个都设为0，剩下的就是基准的了。手动命令行设置也很方便
netsh interface ipv6 set interface 5 metric=0
netsh interface ipv6 set interface 19 metric=0

IPv6 路由表
===========================================================================
活动路由:
 接口跃点数网络目标                网关
  5    256 ::/0                     fe80::96a6:d8ff:fec7:c201
 19     16 ::/0                     fe80::34ff:d7ff:fe5f:d23a
 
 
 #当然也可以直接先删除，然后再设置
 route delete -6 ::/0 if 5
# 重新添加，并指定路由跃点数=10，这个是ipconfig里面网卡的v6网关地址
route add -6 ::/0 fe80::96a6:d8ff:fec7:c201 if 5 metric 10
```

### 对校园内网网段处理：

```shell
route delete 202.118.0.0 mask 255.255.224.0
route delete 202.199.0.0 mask 255.255.240.0
route delete 210.30.192.0 mask 255.255.240.0
route delete 219.216.64.0 mask 255.255.192.0
route delete 58.154.160.0 mask 255.255.224.0
route delete 58.154.192.0 mask 255.255.192.0
route delete 118.202.0.0 mask 255.255.224.0
route delete 118.202.32.0 mask 255.255.240.0
route delete 172.16.0.0 mask 255.240.0.0
route delete 100.64.0.0 mask 255.192.0.0

route add -p 202.118.0.0 mask 255.255.224.0 118.202.10.254 if 5
route add -p 202.199.0.0 mask 255.255.240.0 118.202.10.254 if 5
route add -p 210.30.192.0 mask 255.255.240.0 118.202.10.254 if 5
route add -p 219.216.64.0 mask 255.255.192.0 118.202.10.254 if 5
route add -p 58.154.160.0 mask 255.255.224.0 118.202.10.254 if 5
route add -p 58.154.192.0 mask 255.255.192.0 118.202.10.254 if 5
route add -p 118.202.0.0 mask 255.255.224.0 118.202.10.254 if 5
route add -p 118.202.32.0 mask 255.255.240.0 118.202.10.254 if 5
route add -p 172.16.0.0 mask 255.240.0.0 118.202.10.254 if 5
route add -p 100.64.0.0 mask 255.192.0.0 118.202.10.254 if 5

#118.202.10.254 对应ipconfig查看到的有线网卡的V4网关地址
#子网就10项，全部删除，然后全部添加就好了。需要改的只是118.202.10.254（换个地理位置会改变）和网卡索引5（一般不变）
```

#### 总的来看，命令行只需要改一改有线和无线的V4/V6的路由权重，然后设置校园内网的指定路由就好了

### 注：所学校的校园网网段不一致。使用时需要提前查阅本校校园网网段。并修改代码或者命令行中对应的位置

```shell
["202.118.0.0/19", "202.199.0.0/20", "210.30.192.0/20", "219.216.64.0/18", "58.154.160.0/19", "58.154.192.0/18", "118.202.0.0/19", "118.202.32.0/20", "172.16.0.0/12", "100.64.0.0/10"]
对应	route delete 202.118.0.0 mask 255.255.224.0
对应	route add -p 202.118.0.0 mask 255.255.224.0 118.202.10.254 if 5
也对应
{"202.118.0.0", "255.255.224.0"},
{"202.199.0.0", "255.255.240.0"},
{"210.30.192.0", "255.255.240.0"},
{"219.216.64.0", "255.255.192.0"},
{"58.154.160.0", "255.255.224.0"},
{"58.154.192.0", "255.255.192.0"},
{"118.202.0.0", "255.255.224.0"},
{"118.202.32.0", "255.255.240.0"},
{"172.16.0.0", "255.240.0.0"},
{"100.64.0.0", "255.192.0.0"}
#示例：202.118.0.0/19 对应 {"202.118.0.0", "255.255.224.0"}，左边是网络地址，右边是子网掩码
```

