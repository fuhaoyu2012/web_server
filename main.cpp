// web_server.cpp
// 编译: g++ -std=c++17 web_server.cpp -lws2_32 -o web_server.exe
// 运行: web_server.exe
// 浏览器访问: http://localhost:8080
// 停止: 按 Ctrl+C，服务器会主动清理资源后退出

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <thread>
#include <vector>
#include <csignal>
#include <atomic>

#pragma comment(lib, "ws2_32.lib") // init ws2

// 全局变量 默认socket状态是invalid的
static SOCKET g_listenclient = INVALID_SOCKET;
static std::atomic<bool> g_running{ true };

// 日志系统
void log(const std::string& msg)
{
	// 追踪线程
	std::cout << "[" << std::this_thread::get_id() << "] " << msg << std::endl;
}
// 传文件路径
std::string sReadFile(const std::string& path)
{
	std::ifstream ifs(path); // 读取

	if (!ifs) return ""; // 若读取失败
	std::ostringstream ss;

	// 返回std::streambuf* 
	// std::streambuf 有get area and put area
	// 这个操作是直接将get area的缓冲区数据直接输出到put eara 到EOF
	ss << ifs.rdbuf(); // 原地 节省内存

	return ss.str(); // 原地析构

}
// 上文的解析
// 解析http
std::string sParsePath(const std::string& result)
{
	// GET /index.html HTTP/1.1这是大概请求
	size_t uFirstSpace = result.find(' ');
	if (uFirstSpace == std::string::npos) return "/";
	size_t uSecondSpace = result.find(' ', uFirstSpace + 1); // 查找下一个空格
	if (uSecondSpace == std::string::npos) return "/";

	return result.substr(uFirstSpace + 1, uSecondSpace - uFirstSpace - 1);
}

// 制定服务器响应的类型
std::string sGetContentType(const std::string& path)
{
	if (path.ends_with(".html") || path.ends_with(".htm"))
		return "text/html";
	
	if (path.ends_with(".css"))
		return "text/css";

	if (path.ends_with(".js"))
		return "application/javascript";

	if (path.ends_with(".png"))
		return "image/png";
	if(path.ends_with(".jpg") || path.ends_with(".jpeg"))
		return "image/jpeg";
	if (path.ends_with(".gif"))
		return "image/gif";
	if (path.ends_with(".ico"))
		return "image/x-icon";

	// 如果都没有以上
	return "text/plain"; // 防止xss注入
	// 只显示文本，不会解析、渲染脚本
}

// 构造http响应

std::string sBuildResponse(int iStatusCode, const std::string& sContentType, const std::string& sBody)
{
	std::string text = (iStatusCode == 200) ? "OK" : "Not Found";
	std::string response = "HTTP/1.1 " + std::to_string(iStatusCode) + " " + text + "\r\n";
	response += "Content-Type: " + sContentType + "; charset=utf-8\r\n";
	response += "Content-Length: " + std::to_string(sBody.size()) + "\r\n";
	response += "Connection: close\r\n";
	response += "\r\n";
	response += sBody;

	return response;
}

// 处理单个客户
void nHandleClient(SOCKET ClientSocket)
{
	char buf[4096] = { 0 }; // 4兆缓冲字节

	// -1 给'\0'留个位置
	int ClientReceived = recv(ClientSocket, buf, sizeof(buf) - 1, 0);

	if (ClientReceived <= 0)
	{
		log("Error: recv失败或客户端断开连接");
		closesocket(ClientSocket); // 释放套接字
		return;
	}

	std::string requests(buf); // 读取字符
	std::string path = sParsePath(requests);

	log("[请求]" + path);
	// std::cout << "[" << std::this_thread::get_id() << "] 请求" << path << std::endl;

	if (path == "/") path = "/index.html"; // 默认首页

	std::string filepath = (path.length() > 1) ? path.substr(1) : "index.html";
	std::string body = sReadFile(filepath); // 读取文件内容
	std::string response;

	if (body.empty())
	{
		log("404: " + filepath);
		body = "<h1>404 Not Found</h1><p>文件不存在" + path + "</p>";
		response = sBuildResponse(404, "text/html", body);
	}
	else
	{
		log("200: " + filepath + "(" + std::to_string(body.size()) + ")");
		response = sBuildResponse(200, sGetContentType(path), body);
	}

	send(ClientSocket, response.c_str(), (int)response.size(), 0);

	closesocket(ClientSocket);
}

// 为了有效释放资源用原子bool

void nSingalHandle(int sig)
{
	log("\n[信号] 收到中断信号(Ctrl + c) 正在关闭服务器释放资源...");
	g_running = false;

	// 关闭监听
	if (g_listenclient != INVALID_SOCKET)
	{
		closesocket(g_listenclient);
		g_listenclient = INVALID_SOCKET;
	}
}
int main()
{
	// 设置控制台为 UTF-8
	// SetConsoleOutputCP(CP_UTF8);

	// 初始化信号
	signal(SIGINT, nSingalHandle);

	// 初始化winsock
	WSADATA wsaData; // 版本2.2
	int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (result != 0)
	{
		std::cerr << "Error: WSAstartup 失败! "<< WSAGetLastError() << std::endl;
		WSACleanup(); // 清理
		return 1;
	}

	// 协议族 协议类型
	SOCKET ListenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	if (ListenSocket == INVALID_SOCKET)
	{
		std::cerr << "Error: 创建套接字失败!" << WSAGetLastError() <<std::endl;
		return 1;

	}
	g_listenclient = ListenSocket; // 保存到全局 利于信号处理

	sockaddr_in Server_Addr = {};

	Server_Addr.sin_family = AF_INET;
	Server_Addr.sin_addr.s_addr = INADDR_ANY; // 接受任何
	Server_Addr.sin_port = htons(8080); // 8080端口

	if (bind(ListenSocket, (sockaddr*)&Server_Addr, sizeof(Server_Addr)) == SOCKET_ERROR)
	{
		std::cerr << "Error: bind 失败！" << WSAGetLastError() << std::endl;
		closesocket(ListenSocket);
		WSACleanup();
		return 1;
	}

	if (listen(ListenSocket, SOMAXCONN) == SOCKET_ERROR)
	{
		std::cerr << "Error: 监听失败！" << WSAGetLastError() << std::endl;
		closesocket(ListenSocket);
		WSACleanup();
		return 1;
	}

	std::cout << "========================================" << std::endl;
	std::cout << "  服务器启动成功！" << std::endl;
	std::cout << "  访问: http://localhost:8080" << std::endl;
	std::cout << "  停止: 按 Ctrl+C" << std::endl;
	std::cout << "========================================" << std::endl;

	while (g_running)
	{
		sockaddr_in Client_addr = {};
		int ClientLen = sizeof(Client_addr);

		SOCKET ClientSocket = accept(ListenSocket, (sockaddr*)& Client_addr, &ClientLen);

		// 查看是否接受信号
		if (!g_running) break;

		if (ClientSocket == INVALID_SOCKET)
		{
			if (!g_running) break; // 判断是不是接收到了信号才INVALID_SOCKET

			std::cerr << "Error: accept 失败" << WSAGetLastError() << std::endl;
			continue;
		}

		// 创建新的线程 调用0 function 把1 传进去
		std::thread clientThread(nHandleClient, ClientSocket); // 每一个连接展开一个线程
		clientThread.detach(); // 剥夺线程控制权
	}

	std::cout << "[清理] 正在释放资源..." << std::endl;
	if (ListenSocket != INVALID_SOCKET)
	{
		ListenSocket == INVALID_SOCKET;
		closesocket(ListenSocket);
	}

	WSACleanup();
	// std::cout << "[完成] 服务器安全关闭! " << std::endl;
	log("服务器已关闭...");

	return 0;
}