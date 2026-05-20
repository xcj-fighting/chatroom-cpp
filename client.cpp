#include <iostream>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstring>
#include <mutex>
#include <thread>
#include <atomic>
#include <unistd.h>

#define MAX_LEN 200
#define NUM_COLORS 6

int client_socket;
std::atomic<bool> exit_flag{false};
std::mutex cout_metux;
std::thread t_send, t_recv;

std::string def_col = "\033[0m";
std::string colors[] = {"\033[31m", "\033[32m", "\033[33m", "\033[34m", "\033[35m", "\033[36m"};

void catch_ctrl_c(int signal);
void erase_text(int cnt);
void send_message(int sockfd);
void recv_message(int sockfd);

static bool recv_all(int sockfd, void* buf, size_t len)
{
    size_t total = 0;
    while (total < len)
    {
        ssize_t n = recv(sockfd, static_cast<char*>(buf) + total, len - total, 0);
        if (n <= 0) return false;
        total += static_cast<size_t>(n);
    }
    return true;
}

int main()
{
    client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket == -1)
    {
        perror("socket: ");
        exit(1);
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(10000);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    // sin_zero = 填充位（占位用的），只为了让结构体长度对齐，没有任何实际功能。
    memset(&server_addr.sin_zero, 0, 8);

    if (connect(client_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1)
    {
        perror("client connect failed: ");
        exit(-1);
    }

    char name[MAX_LEN];
    std::cout << "Enter your name: ";
    // getline会自动在末尾补'\0'
    std::cin.getline(name, MAX_LEN);

    // 0 表示阻塞发送
    if (send(client_socket, name, sizeof(name), 0) == -1)
    {
        perror("client send name failed: ");
        exit(1);
    }

    std::cout << colors[NUM_COLORS - 1] << "\n\t  ====== Welcome to the chat-room ======   " << std::endl << def_col;

    // 发送线程
    t_send = std::thread(send_message, client_socket);
    t_recv = std::thread(recv_message, client_socket);

    if (t_send.joinable()) t_send.join();
    if (t_recv.joinable()) t_recv.join();

    close(client_socket);

    return 0;
}

// 
void erase_text(int cnt)
{
    std::lock_guard<std::mutex> lock(cout_metux);
    
	char back_space=8;
	for(int i=0; i<cnt; i++)
	{
		std::cout << back_space;
	}	
}

// 发送消息给所有人
void send_message(int sockfd)
{
    while(1)
    {
        {
            std::lock_guard<std::mutex> lock(cout_metux);
            std::cout << colors[1] << "You: " << def_col;
        }
        
        char str[MAX_LEN];
        std::cin.getline(str, MAX_LEN);
        if (send(sockfd, str, sizeof(str), 0) == -1)
        {
            perror("send_message error: ");
            return;
        }

        if (strcmp(str, "#exit") == 0)
        {
            exit_flag = true;
            shutdown(sockfd, SHUT_RDWR);
            // close(sockfd);
            break;
        }
    }
}

// 接收别人发送的消息
void recv_message(int sockfd)
{
    while(1)
    {
        if (exit_flag) break;

        char name[MAX_LEN], str[MAX_LEN];
        int color_code;

        if (!recv_all(sockfd, name, sizeof(name)))
            break;
        if (!recv_all(sockfd, &color_code, sizeof(color_code)))
            break;
        if (!recv_all(sockfd, str, sizeof(str)))
            break;

        erase_text(6);

        {
            std::lock_guard<std::mutex> lock(cout_metux);

            if (strcmp(name, "#NULL") != 0)
                std::cout << colors[color_code%6] << name << ": " << def_col << str << std::endl;
            else
                std::cout << colors[color_code%6] << str << std::endl;    // 服务端发送的消息，无用户名
            
            std::cout << colors[1] << "You: " << def_col;
            fflush(stdout);
        }
    }
}
