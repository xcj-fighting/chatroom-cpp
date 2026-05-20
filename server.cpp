#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <cstring>
#include <mutex>
#include <vector>
#include <thread>
#include <unistd.h>

#define MAX_LEN 200

using namespace std;

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

struct Client
{
    int id;
    string name;
    int sockfd;
    std::thread th;
    bool alive = true;
};

std::mutex clients_mtx;
std::mutex cout_mtx;
int client_count = 0;
std::vector<Client> clients;

string def_col="\033[0m";
string colors[]={"\033[31m", "\033[32m", "\033[33m", "\033[34m", "\033[35m","\033[36m"};

void set_name(int id, char name[]);
void broadcast_message(string messages, int sender_id);
void shared_print(string str, bool endLine = true);
void broadcast_message(int num, int sender_id);
void handle_client(int client_sockfd, int id);

int main()
{
    clients.reserve(10);

    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1)
    {
        perror("Failed to create server socket");
        exit(1);
    }

    // 允许端口复用
    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1)
    {
        perror("reuse addr error: ");
        exit(1);
    }

    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(10000);
    memset(server_addr.sin_zero, 0, sizeof(server_addr.sin_zero));

    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1)
    {
        perror("bind error: ");
        exit(1);
    }

    if (listen(server_socket, 1024) == -1)
    {
        perror("listen error: ");
        exit(-1);
    }

    int client_sockfd;
    sockaddr_in client_addr;
    unsigned int len = sizeof(client_addr);

    while(1)
    {
        client_sockfd = accept(server_socket, (struct sockaddr*)&client_addr, &len);
        if (client_sockfd == -1)
        {
            perror("accept error:");
            exit(1);
        }

        std::lock_guard<std::mutex> lock(clients_mtx);

        // clean inactive client
        for (auto it = clients.begin(); it != clients.end();) {
            if (!it->alive) {
                it = clients.erase(it);  // 主线程安全删除
            } else {
                ++it;
            }
        }

        std::thread t(handle_client, client_sockfd, ++client_count);
        t.detach();
        clients.push_back({client_count, "Anonymous", client_sockfd, std::move(t)});
    }

    for (int i=0;i < clients.size();i++)
    {
        Client &client = clients.at(i);
        if (client.th.joinable())
            client.th.join();
    }

    close(server_socket);
    return 0;
}

// set client's name
void set_name(int id, char name[])
{
    std::lock_guard<std::mutex> lock(clients_mtx);
    for (int i=0;i < clients.size();i++)
    {
        if (clients[i].id == id)
        {
            clients[i].name = string(name);
            break;
        }
    }
}

// 线程安全cout
void shared_print(string str, bool endLine)
{
    std::lock_guard<std::mutex> lock(cout_mtx);
    std::cout << str;
    if (endLine) std::cout << std::endl;
    else std::cout << "\n";
}

// Broadcast message to all clients except the sender
void broadcast_message(string message, int sender_id)
{
    std::lock_guard<std::mutex> lock(clients_mtx);
    char temp[MAX_LEN];
    memset(temp, 0, MAX_LEN);
    strncpy(temp, message.c_str(), MAX_LEN - 1);
    for (int i=0;i < clients.size();i++)
    {
        if (clients.at(i).id == sender_id) continue;
        if (!clients.at(i).alive) continue;
        if (send(clients[i].sockfd, temp, MAX_LEN, 0) == -1)
        {
            std::cout << "Failed to broadcast message to " << i << '\n';
            perror("");
        }
    }
}

// Broadcast num to all clients except the sender
void broadcast_message(int num, int sender_id)
{
    std::lock_guard<std::mutex> lock(clients_mtx);
    for (int i=0;i < clients.size();i++)
    {
        if (clients.at(i).id == sender_id) continue;
        if (!clients.at(i).alive) continue;
        if (send(clients[i].sockfd, &num, sizeof(num), 0) == -1)
        {
            std::cout << "Failed to broadcast num to " << i << '\n';
            perror("");
        }
    }
}

void end_connection(int id)
{
    std::lock_guard<std::mutex> lock(clients_mtx);
    for (int i=0;i < clients.size();i++)
    {
        if (clients[i].id == id)
        {
            close(clients.at(i).sockfd);
            clients.at(i).alive = false;
            break;
        }
    }
}

void handle_client(int client_sockfd, int id)
{
    char name[MAX_LEN], str[MAX_LEN];
    if (!recv_all(client_sockfd, name, sizeof(name)))
    {
        perror("Failed to recv client name: ");
        return;
    }

    set_name(id, name);

    // Display welcome message
    string welcome_msg = string(name) + " has joined";
    // send name
    broadcast_message("#NULL", id);
    // send id
    broadcast_message(id, id);
    // send welcome msg
    broadcast_message(welcome_msg, id);
    shared_print(colors[id%6] + welcome_msg + def_col);

    while(1)
    {
        if (!recv_all(client_sockfd, str, sizeof(str)))
        {
            std::cout << "id: " << id << " recv error: ";
            perror("");
            return;
        }

        // 客户端断开消息
        if (strcmp(str, "#exit") == 0)
        {
            // Display leaving message
            std::string message = string(name) + " has left";
            broadcast_message("#NULL", id);
            broadcast_message(id, id);
            broadcast_message(message, id);
            shared_print(colors[id%6] + message + def_col);
            end_connection(id);
            return;
        }

        // 普通消息
        broadcast_message(name, id);
        broadcast_message(id, id);
        broadcast_message(str, id);
        shared_print(colors[id%6] + name + ": " + str + def_col);
    }
}