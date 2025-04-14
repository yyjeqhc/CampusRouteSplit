#include <winsock2.h>  // 必须在windows.h之前包含
#include <netioapi.h>
#include <windows.h>
#include <iphlpapi.h>
#include <iostream>
#include <vector>
#include <string>
#include <ws2tcpip.h>
#include <wininet.h>
#include <sstream>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "wininet.lib")

struct NetworkAdapter {
    std::string name;
    std::string description;
    DWORD index;
    std::string gateway;
    std::string gateway_6;
    bool isWired;
    bool isWireless;
    bool isConnected;
};

// 检查指定URL的连接性
bool CheckConnectivity(const char* host) {
    HINTERNET hInternet = InternetOpenA("NetworkRouteManager", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    if (!hInternet) {
        std::cerr << "InternetOpen failed: " << GetLastError() << std::endl;
        return false;
    }

    HINTERNET hConnect = InternetOpenUrlA(hInternet, host, NULL, 0, 
        INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_UI, 0);
    
    bool result = (hConnect != NULL);
    
    if (hConnect) InternetCloseHandle(hConnect);
    InternetCloseHandle(hInternet);
    
    return result;
}

// 获取所有网络适配器信息
std::vector<NetworkAdapter> GetNetworkAdapters() {
    std::vector<NetworkAdapter> adapters;
    
    // 定义标志常量，原本应该由头文件提供
    const ULONG FLAGS_INCLUDE_PREFIX = 0x0010;  // 改名，避免与可能的宏定义冲突
    const ULONG FLAGS_INCLUDE_GATEWAYS = 0x0080;  // 改名，避免与可能的宏定义冲突
    
    ULONG flags = FLAGS_INCLUDE_PREFIX | FLAGS_INCLUDE_GATEWAYS;
    ULONG outBufLen = 0;
    
    // 获取所需缓冲区大小
    GetAdaptersAddresses(AF_UNSPEC, flags, NULL, NULL, &outBufLen);
    std::vector<BYTE> buffer(outBufLen);
    PIP_ADAPTER_ADDRESSES pAddresses = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data());

    if (GetAdaptersAddresses(AF_UNSPEC, flags, NULL, pAddresses, &outBufLen) != NO_ERROR) {
        std::cerr << "Failed to get adapter addresses" << std::endl;
        return adapters;
    }

    for (PIP_ADAPTER_ADDRESSES pCurr = pAddresses; pCurr != NULL; pCurr = pCurr->Next) {
        if (pCurr->OperStatus == IfOperStatusUp) {
            NetworkAdapter adapter;
            adapter.name = std::string(pCurr->AdapterName);
            
            // 转换宽字符描述为多字节
            char descBuffer[256] = {0};
            WideCharToMultiByte(CP_ACP, 0, pCurr->Description, -1, 
                                descBuffer, sizeof(descBuffer), NULL, NULL);
            adapter.description = std::string(descBuffer);
            
            adapter.index = pCurr->IfIndex;
            adapter.isConnected = (pCurr->OperStatus == IfOperStatusUp);
            adapter.isWired = (pCurr->IfType == IF_TYPE_ETHERNET_CSMACD);
            adapter.isWireless = (pCurr->IfType == IF_TYPE_IEEE80211);
            
            // 获取网关
            for (PIP_ADAPTER_GATEWAY_ADDRESS gateway = pCurr->FirstGatewayAddress; 
                 gateway != NULL; 
                 gateway = gateway->Next) {
                 
                if (gateway->Address.lpSockaddr->sa_family == AF_INET) {
                    char gatewayStr[INET_ADDRSTRLEN];
                    struct sockaddr_in* sockaddr = (struct sockaddr_in*)gateway->Address.lpSockaddr;
                    inet_ntop(AF_INET, &(sockaddr->sin_addr), gatewayStr, INET_ADDRSTRLEN);
                    adapter.gateway = std::string(gatewayStr);
                } else if(gateway->Address.lpSockaddr->sa_family == AF_INET6) {
                    char gatewayStr[INET6_ADDRSTRLEN];
                    struct sockaddr_in6* sockaddr = (struct sockaddr_in6*)gateway->Address.lpSockaddr;
                    inet_ntop(AF_INET6, &(sockaddr->sin6_addr), gatewayStr, INET6_ADDRSTRLEN);
                    adapter.gateway_6 = std::string(gatewayStr);
                }
            }
            
            // 只添加有网关的活动适配器
            if (!adapter.gateway.empty()) {
                adapters.push_back(adapter);
            }
        }
    }
    
    return adapters;
}

// 检查有线和无线网络是否都已连接
bool CheckNetworkConnections(NetworkAdapter& wiredAdapter, NetworkAdapter& wirelessAdapter) {
    auto adapters = GetNetworkAdapters();
    
    bool wiredFound = false;
    bool wirelessFound = false;
    
    // 打印所有可用适配器
    // std::cout << "Available network adapters:" << std::endl;
    // for (const auto& adapter : adapters) {
    //     std::cout << "  - " << adapter.description 
    //               << " (Type: " << (adapter.isWired ? "Wired" : (adapter.isWireless ? "Wireless" : "Other"))
    //               << ", Gateway: " << adapter.gateway 
    //               << ", Index: " << adapter.index << ")" << std::endl;
    // }
    
    for (const auto& adapter : adapters) {
        if (adapter.isConnected) {
            // 排除虚拟适配器
            bool isVirtual = 
                adapter.description.find("ZeroTier") != std::string::npos ||
                adapter.description.find("Virtual") != std::string::npos ||
                adapter.description.find("VMware") != std::string::npos ||
                adapter.description.find("VirtualBox") != std::string::npos ||
                adapter.description.find("TAP") != std::string::npos;
                
            if (adapter.isWired && !isVirtual) {
                wiredAdapter = adapter;
                wiredFound = true;
                std::cout << "Physical wired connection found: " << adapter.description 
                          << " (Gateway: " << adapter.gateway 
                          << ", Interface Index: " << adapter.index << ")" << std::endl;
            } else if (adapter.isWireless) {
                wirelessAdapter = adapter;
                wirelessFound = true;
                std::cout << "Wireless connection found: " << adapter.description 
                          << " (Gateway: " << adapter.gateway 
                          << ", Interface Index: " << adapter.index << ")" << std::endl;
            }
        }
    }
    
    return wiredFound && wirelessFound;
}

// 使用WinAPI删除路由 - 改进版
bool DeleteRoute(const std::string& destination, const std::string& mask) {
    DWORD destAddr = 0;
    DWORD maskAddr = 0;
    
    inet_pton(AF_INET, destination.c_str(), &destAddr);
    inet_pton(AF_INET, mask.c_str(), &maskAddr);
    
    // 获取路由表
    PMIB_IPFORWARDTABLE pIpForwardTable = NULL;
    DWORD dwSize = 0;
    
    // 获取所需缓冲区大小
    if (GetIpForwardTable(NULL, &dwSize, TRUE) == ERROR_INSUFFICIENT_BUFFER) {
        pIpForwardTable = (PMIB_IPFORWARDTABLE)malloc(dwSize);
    }
    
    if (pIpForwardTable == NULL) {
        std::cerr << "Memory allocation failed for IP Forward Table" << std::endl;
        return false;
    }
    
    // 获取路由表内容
    if (GetIpForwardTable(pIpForwardTable, &dwSize, TRUE) != NO_ERROR) {
        std::cerr << "GetIpForwardTable failed" << std::endl;
        free(pIpForwardTable);
        return false;
    }
    
    bool routeFound = false;
    MIB_IPFORWARDROW routeToDelete;
    // std::cout<<"have"<<destAddr<<" "<<maskAddr<<std::endl;
    // 在路由表中查找匹配的路由
    for (DWORD i = 0; i < pIpForwardTable->dwNumEntries; i++) {
        if (pIpForwardTable->table[i].dwForwardDest == destAddr &&
            pIpForwardTable->table[i].dwForwardMask == maskAddr) {
            routeToDelete = pIpForwardTable->table[i];
            routeFound = true;
            
            break;
        }
    }
    
    free(pIpForwardTable);
    
    // 如果找到匹配的路由，则尝试删除
    if (routeFound) {
        DWORD result = DeleteIpForwardEntry(&routeToDelete);
        if (result != NO_ERROR) {
            std::cerr << "Failed to delete route " << destination << " mask " << mask 
                      << ", error code: " << result << std::endl;
            return false;
        }
        
        // 显示删除的路由信息
        struct in_addr destIP, maskIP, nextHop;
        destIP.s_addr = routeToDelete.dwForwardDest;
        maskIP.s_addr = routeToDelete.dwForwardMask;
        nextHop.s_addr = routeToDelete.dwForwardNextHop;
        
        char destStr[INET_ADDRSTRLEN], maskStr[INET_ADDRSTRLEN], nextHopStr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &destIP, destStr, INET_ADDRSTRLEN);
        inet_ntop(AF_INET, &maskIP, maskStr, INET_ADDRSTRLEN);
        inet_ntop(AF_INET, &nextHop, nextHopStr, INET_ADDRSTRLEN);
        
        // std::cout << "Route deleted: " << destStr << " mask " << maskStr 
        //           << " gateway " << nextHopStr << " interface " << routeToDelete.dwForwardIfIndex << std::endl;
        
        return true;
    } 
    return false;
    // else {
    //     std::cout << "Route not found: " << destination << " mask " << mask << std::endl;
    //     return false;
    // }
}
// 使用WinAPI添加永久路由 - 改进版
bool AddRoute(const std::string& destination, const std::string& mask, 
    const std::string& gateway, DWORD interfaceIndex) {
    // 转换IP地址 - 确保使用正确的网络字节顺序
    struct in_addr destAddr, maskAddr, gatewayAddr;
    
    // 使用inet_pton进行转换，它更安全、更现代
    if (inet_pton(AF_INET, destination.c_str(), &destAddr) != 1 ||
        inet_pton(AF_INET, mask.c_str(), &maskAddr) != 1 || 
        inet_pton(AF_INET, gateway.c_str(), &gatewayAddr) != 1) {
        std::cerr << "Invalid IP address format" << std::endl;
        return false;
    }

    // 检查目标网络与掩码是否匹配
    if ((destAddr.s_addr & maskAddr.s_addr) != destAddr.s_addr) {
        std::cerr << "Destination and mask do not match" << std::endl;
        return false;
    }

    // 先尝试删除可能存在的相同路由，不用了，前面的DeleteRoutes可以确定删除成功
    // DeleteRoute(destination, mask);

    /*
    属性的设置参阅
    https://learn.microsoft.com/zh-cn/windows/win32/api/iphlpapi/nf-iphlpapi-createipforwardentry
    */
    MIB_IPFORWARDROW route;
    // memset(&route, 0, sizeof(route));
    route.dwForwardDest = destAddr.s_addr;
    route.dwForwardMask = maskAddr.s_addr;
    route.dwForwardNextHop = gatewayAddr.s_addr;
    route.dwForwardIfIndex = interfaceIndex;
    route.dwForwardProto = MIB_IPPROTO_NETMGMT;

    //不能设置，设置了就160错误
    // route.dwForwardMetric1 = 20;

    // 添加路由
    DWORD result = CreateIpForwardEntry(&route);

    if (result != NO_ERROR) {
        // 如果添加失败，尝试使用系统命令
        // 这个添加的是永久路由，chatgpt说是保存在注册表，尽量不用system来调用，保留，仅供学习使用
        // if (result == ERROR_BAD_ARGUMENTS || result == ERROR_INVALID_PARAMETER) {
        // std::string cmd = "route add -p " + destination + " mask " + mask + 
        //                 " " + gateway + " if " + std::to_string(interfaceIndex);
        // std::cout << "API failed, executing system command: " << cmd << std::endl;
        // int sysResult = system(cmd.c_str());
        // if (sysResult == 0) {
        //     std::cout << "Route added via system command: " << destination << " mask " << mask 
        //                 << " gateway " << gateway << " interface " << interfaceIndex << std::endl;
        //     return true;
        // }
        std::cerr << "Failed to add route " << destination << " mask " << mask 
            << " gateway " << gateway << " interface " << interfaceIndex 
            << ", error code: " << result << std::endl;
        return false;
    }

    

    // std::cout << "Route added: " << destination << " mask " << mask 
    //     << " gateway " << gateway << " interface " << interfaceIndex << std::endl;
    return true;
}


// 删除特定路由列表
void DeleteRoutes() {
    const std::vector<std::pair<std::string, std::string>> routesToDelete = {
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
    };

    // 先使用系统命令显示当前的路由表
    // std::cout << "Displaying current routing table..." << std::endl;
    // system("route print > routes_before.txt");
    
    for (const auto& route : routesToDelete) {
        // 使用系统命令删除路由，可能更可靠
        // std::string cmd = "route delete " + route.first + " mask " + route.second;
        // std::cout << "Executing: " << cmd << std::endl;
        // system(cmd.c_str());
        
        // 也尝试使用API删除
        DeleteRoute(route.first, route.second);
    }
}

// 添加特定路由列表
void AddRoutes(const std::string& gateway, DWORD interfaceIndex) {
    const std::vector<std::pair<std::string, std::string>> routesToAdd = {
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
    };

    for (const auto& route : routesToAdd) {
        // 先使用API添加
        if (!AddRoute(route.first, route.second, gateway, interfaceIndex)) {
            // 如果失败，尝试使用系统命令
            // std::string cmd = "route add -p " + route.first + " mask " + route.second + 
            //                  " " + gateway + " if " + std::to_string(interfaceIndex);
            // std::cout << "Executing: " << cmd << std::endl;
            // system(cmd.c_str());
        }
    }
    
    // 显示添加后的路由表
    // std::cout << "Displaying updated routing table..." << std::endl;
    // system("route print > routes_after.txt");
}

// 调整默认路由优先级，使无线网络优先级高于有线网络
void AdjustDefaultRoutePriority(const NetworkAdapter& wiredAdapter, 
                               const NetworkAdapter& wirelessAdapter) {
    // 获取当前路由表
    PMIB_IPFORWARDTABLE pIpForwardTable = NULL;
    DWORD dwSize = 0;
    
    // 获取所需缓冲区大小
    if (GetIpForwardTable(NULL, &dwSize, TRUE) == ERROR_INSUFFICIENT_BUFFER) {
        pIpForwardTable = (PMIB_IPFORWARDTABLE)malloc(dwSize);
    }
    
    if (pIpForwardTable == NULL) {
        std::cerr << "Memory allocation failed for IP Forward Table" << std::endl;
        return;
    }
    
    // 获取路由表
    if (GetIpForwardTable(pIpForwardTable, &dwSize, TRUE) != NO_ERROR) {
        std::cerr << "GetIpForwardTable failed" << std::endl;
        free(pIpForwardTable);
        return;
    }
    
    // 查找并修改默认路由的度量值
    for (DWORD i = 0; i < pIpForwardTable->dwNumEntries; i++) {
        // 检查是否为默认路由(0.0.0.0)
        if (pIpForwardTable->table[i].dwForwardDest == 0) {
            MIB_IPFORWARDROW route = pIpForwardTable->table[i];
            
            // 如果是有线网络接口的默认路由，将度量值设置大（较低优先级）
            if (route.dwForwardIfIndex == wiredAdapter.index) {
                route.dwForwardMetric1 = 50;
                SetIpForwardEntry(&route);
            }
            
            // 如果是无线网络接口的默认路由，将度量值设置小（较高优先级）
            //使用route print查看，和设置的值不一样，也很正常，只要二者的大小关系符合期望，就好了
            else if (route.dwForwardIfIndex == wirelessAdapter.index) {
                route.dwForwardMetric1 = 30;
                SetIpForwardEntry(&route);
            }
        }
    }
    free(pIpForwardTable);
}


// 判断网络适配器是否有公网IPv6地址并调整IPv6路由优先级
bool AdjustIPv6RoutePriority(const NetworkAdapter& wiredAdapter, 
    const NetworkAdapter& wirelessAdapter) {
    // std::cout << "Checking IPv6 connectivity..." << std::endl;

    // 获取IPv6地址信息
    bool wiredHasPublicIPv6 = false;
    bool wirelessHasPublicIPv6 = false;
    std::string wiredLinkLocalAddress;
    std::string wirelessLinkLocalAddress;

    // 分配内存以存储IPv6地址信息
    IP_ADAPTER_ADDRESSES* pAddresses = NULL;
    ULONG outBufLen = 0;

    // 先获取所需缓冲区大小
    GetAdaptersAddresses(AF_INET6, GAA_FLAG_INCLUDE_PREFIX, NULL, NULL, &outBufLen);
    pAddresses = (IP_ADAPTER_ADDRESSES*)malloc(outBufLen);

    if (pAddresses == NULL) {
        std::cerr << "Memory allocation failed for adapter addresses" << std::endl;
        return false;
    }

    // 获取适配器地址
    if (GetAdaptersAddresses(AF_INET6, GAA_FLAG_INCLUDE_PREFIX, NULL, pAddresses, &outBufLen) != NO_ERROR) {
        std::cerr << "GetAdaptersAddresses failed for IPv6" << std::endl;
        free(pAddresses);
        return false;
    }

    // 遍历所有网络适配器
    for (IP_ADAPTER_ADDRESSES* pCurrent = pAddresses; pCurrent != NULL; pCurrent = pCurrent->Next) {
        // 检查是否与我们的有线或无线适配器匹配
        if (pCurrent->IfIndex == wiredAdapter.index || pCurrent->IfIndex == wirelessAdapter.index) {
            bool isWired = (pCurrent->IfIndex == wiredAdapter.index);

            // 遍历该适配器的所有IPv6地址
            for (IP_ADAPTER_UNICAST_ADDRESS* pUnicast = pCurrent->FirstUnicastAddress; 
                pUnicast != NULL; 
                pUnicast = pUnicast->Next) {

                // 只检查IPv6地址
                if (pUnicast->Address.lpSockaddr->sa_family == AF_INET6) {
                    sockaddr_in6* sockaddr = (sockaddr_in6*)pUnicast->Address.lpSockaddr;
                    char ipv6Str[INET6_ADDRSTRLEN] = {0};

                    // 转换为字符串形式的IPv6地址
                    inet_ntop(AF_INET6, &(sockaddr->sin6_addr), ipv6Str, INET6_ADDRSTRLEN);
                    std::string ipv6Address(ipv6Str);

                    // 检查是否是公网IPv6地址或链路本地地址
                    bool isLinkLocal = ipv6Address.compare(0, 4, "fe80") == 0;
                    bool isPrivate = 
                        isLinkLocal || // 链路本地地址
                        ipv6Address.compare(0, 4, "fc00") == 0 || // 唯一本地地址
                        ipv6Address.compare(0, 4, "fd00") == 0 || // 唯一本地地址
                        ipv6Address.compare(0, 3, "::1") == 0;    // 回环地址

                    bool isTemporary = ((pUnicast->Flags & IP_ADAPTER_ADDRESS_TRANSIENT) != 0);

                    // 保存链路本地地址，后面用于设置路由
                    if (isLinkLocal) {
                        if (isWired) {
                            wiredLinkLocalAddress = ipv6Address;
                            // std::cout << "Wired adapter link-local IPv6: " << ipv6Address << std::endl;
                        } else {
                            wirelessLinkLocalAddress = ipv6Address;
                            // std::cout << "Wireless adapter link-local IPv6: " << ipv6Address << std::endl;
                        }
                    }

                    if (!isPrivate) {
                        if (isWired) {
                            wiredHasPublicIPv6 = true;
                            // std::cout << "Wired adapter has public IPv6: " << ipv6Address 
                            //         << (isTemporary ? " (temporary)" : "") << std::endl;
                        } else {
                            wirelessHasPublicIPv6 = true;
                            // std::cout << "Wireless adapter has public IPv6: " << ipv6Address 
                            //         << (isTemporary ? " (temporary)" : "") << std::endl;
                        }
                    }
                }
            }
        }
    }

    free(pAddresses);

    std::cout << "IPv6 connectivity status: Wired=" 
        << (wiredHasPublicIPv6 ? "Yes" : "No") 
        << ", Wireless=" 
        << (wirelessHasPublicIPv6 ? "Yes" : "No") << std::endl;

    // 如果两个适配器都有公网IPv6地址，则调整路由优先级
    if ((wiredHasPublicIPv6 || !wiredLinkLocalAddress.empty()) && 
        (wirelessHasPublicIPv6 || !wirelessLinkLocalAddress.empty())) {
        // std::cout << "Adjusting IPv6 route priorities..." << std::endl;

        //sonnet3.7的知识过时了？？？它竟然说v6不能使用win32api管理，所以刚开始用的system调用
        PMIB_IPFORWARD_TABLE2 pTable = nullptr;
        if (GetIpForwardTable2(AF_INET6, &pTable) == NO_ERROR) {
            for (ULONG i = 0; i < pTable->NumEntries; i++) {
                MIB_IPFORWARD_ROW2& row = pTable->Table[i];

                if ( row.DestinationPrefix.Prefix.si_family == AF_INET6 &&
                    row.DestinationPrefix.PrefixLength == 0) {
                    if(row.InterfaceIndex == wiredAdapter.index) {
                        MIB_IPFORWARD_ROW2 rowToDelete = row;
                        row.Metric = 20;

                        if (SetIpForwardEntry2(&row) == NO_ERROR) {
                            std::cout << "Updated metric to " << row.Metric << " for interface " <<wiredAdapter.index << std::endl;
                        } else {
                            std::cerr << "Failed to update metric for interface " << std::endl;
                        }
                    } else if(row.InterfaceIndex == wirelessAdapter.index) {
                        MIB_IPFORWARD_ROW2 rowToDelete = row;
                        row.Metric = 60;

                        if (SetIpForwardEntry2(&row) == NO_ERROR) {
                            std::cout << "Updated metric to " << row.Metric << " for interface " <<wirelessAdapter.index << std::endl;
                        } else {
                            std::cerr << "Failed to update metric for interface " << std::endl;
                        }
                    }
                    //一开始想删除，然后再增加，后来发现，直接改原来的值就行了，省事
                    // if (DeleteIpForwardEntry2(&rowToDelete) == NO_ERROR) {
                    //     std::cout << "Deleted IPv6 default route for ifIndex " << row.InterfaceIndex << std::endl;
                    // } else {
                    //     std::cerr << "Failed to delete route for ifIndex " << row.InterfaceIndex << std::endl;
                    // }
                }
            }
            FreeMibTable(pTable);
            }
        // 删除现有的默认IPv6路由
        // std::string cmd = "route delete -6 ::/0 if " + 
        //                   std::to_string(wiredAdapter.index);
        // std::cout << "Executing: " << cmd << std::endl;
        // system(cmd.c_str());

        // cmd = "route delete -6 ::/0 if " + 
        //                     std::to_string(wirelessAdapter.index);
        // std::cout << "Executing: " << cmd << std::endl;
        // system(cmd.c_str());

        
        // 为有线网络添加低度量值的IPv6默认路由
        // if (!wiredLinkLocalAddress.empty()) {
        //     cmd = "route add -6 ::/0 " + wiredAdapter.gateway_6 + " if " + 
        //           std::to_string(wiredAdapter.index) + " metric 40";
        //     std::cout << "Executing: " << cmd << std::endl;
        //     system(cmd.c_str());
        // }
        
        // 为无线网络添加高度量值的IPv6默认路由
        // if (!wirelessLinkLocalAddress.empty()) {
        //     cmd = "route add -6 ::/0 " + wirelessAdapter.gateway_6 + " if " + 
        //           std::to_string(wirelessAdapter.index) + " metric 80";
        //     std::cout << "Executing: " << cmd << std::endl;
        //     system(cmd.c_str());
        // }

        // 显示IPv6路由表以确认更改
        // cmd = "route print -6";
        // std::cout << "Displaying IPv6 routes:" << std::endl;
        // system(cmd.c_str());

        // std::cout << "IPv6 route metrics adjusted: wired=10 (higher priority), wireless=20 (lower priority)" << std::endl;
        return true;
    } else if (wiredHasPublicIPv6 || !wiredLinkLocalAddress.empty()) {
        // std::cout << "Only wired adapter has IPv6. No need to adjust route priorities." << std::endl;
        return true;
    } else if (wirelessHasPublicIPv6 || !wirelessLinkLocalAddress.empty()) {
        // std::cout << "Only wireless adapter has IPv6. No need to adjust route priorities." << std::endl;
        return true;
    } else {
        // std::cout << "No IPv6 addresses found. Skipping IPv6 route adjustment." << std::endl;
        return false;
    }
}
int main() {
    // 初始化Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed" << std::endl;
        return 1;
    }
    
    NetworkAdapter wiredAdapter;
    NetworkAdapter wirelessAdapter;
    
    if (!CheckNetworkConnections(wiredAdapter, wirelessAdapter)) {
        std::cerr << "Both wired and wireless connections are not available." << std::endl;
        WSACleanup();
        return 1;
    }
    // std::cout<<wiredAdapter.gateway_6<<" "<<wiredAdapter.gateway<<std::endl;
    // std::cout<<wirelessAdapter.gateway_6<<" "<<wirelessAdapter.gateway<<std::endl;
    // AdjustIPv6RoutePriority(wiredAdapter, wirelessAdapter);

    //临时测试
    // WSACleanup();
    // return 0;
    std::cout << "Both wired and wireless networks are connected." << std::endl;
    
    // 删除现有路由
    // std::cout << "Deleting existing routes..." << std::endl;
    DeleteRoutes();
    
    // 使用有线网络的网关添加新路由
    // std::cout << "Adding routes via wired network gateway..." << std::endl;
    AddRoutes(wiredAdapter.gateway, wiredAdapter.index);
    
    // 调整IPv4默认路由优先级，使无线网络的优先级高于有线网络
    // std::cout << "Adjusting IPv4 default route priorities..." << std::endl;
    AdjustDefaultRoutePriority(wiredAdapter, wirelessAdapter);
    
    // 检查IPv6连接性并调整IPv6默认路由优先级
    // std::cout << "Checking IPv6 connectivity and adjusting priorities..." << std::endl;
    AdjustIPv6RoutePriority(wiredAdapter, wirelessAdapter);
    
    std::cout << "Network route management completed successfully." << std::endl;
    
    WSACleanup();
    return 0;
}