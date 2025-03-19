#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>
#include <thread>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <cstring>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>
#include <fstream>
#include <functional>

using namespace std;

//------------------------------
// Storage Engine (Part 1)
//------------------------------
class StorageEngine
{
private:
    unordered_map<string, string> db;
    mutex db_mutex;

public:
    void set(const string &key, const string &value)
    {
        lock_guard<mutex> lock(db_mutex);
        db[key] = value;
    }
    string get(const string &key)
    {
        lock_guard<mutex> lock(db_mutex);
        return db.count(key) ? db[key] : "null";
    }
    void del(const string &key)
    {
        lock_guard<mutex> lock(db_mutex);
        db.erase(key);
    }
};

//------------------------------
// RESP-2 Encoder/Decoder
//------------------------------
namespace RESP
{
    string encodeSimpleString(const string &str)
    {
        return "+" + str + "\r\n";
    }
    string encodeBulkString(const string &str)
    {
        if (str == "null")
        {
            return "$-1\r\n"; // RESP-2 representation for null
        }
        return "$" + to_string(str.size()) + "\r\n" + str + "\r\n";
    }
    string encodeError(const string &err)
    {
        return "-" + err + "\r\n";
    }
    string encodeInteger(int value)
    {
        return ":" + to_string(value) + "\r\n";
    }
}

//------------------------------
// RESP-2 Command Parser
//------------------------------
// Attempts to parse one complete RESP command (an array) from the buffer.
// If successful, 'tokens' is filled with the command arguments and 'consumed'
// is set to the number of bytes used from the buffer.
// Returns true if a complete command was parsed; otherwise false.
bool tryParseCommand(const string &buffer, vector<string> &tokens, size_t &consumed)
{
    tokens.clear();
    consumed = 0;
    if (buffer.empty() || buffer[0] != '*')
    {
        return false; // Not a valid RESP array command.
    }
    size_t pos = 1;
    size_t crlf = buffer.find("\r\n", pos);
    if (crlf == string::npos)
        return false;
    int numArgs;
    try
    {
        numArgs = stoi(buffer.substr(pos, crlf - pos));
    }
    catch (...)
    {
        return false;
    }
    pos = crlf + 2;
    for (int i = 0; i < numArgs; i++)
    {
        if (pos >= buffer.size() || buffer[pos] != '$')
            return false;
        pos++; // Skip '$'
        crlf = buffer.find("\r\n", pos);
        if (crlf == string::npos)
            return false;
        int argLen;
        try
        {
            argLen = stoi(buffer.substr(pos, crlf - pos));
        }
        catch (...)
        {
            return false;
        }
        pos = crlf + 2;
        if (pos + argLen + 2 > buffer.size())
            return false;
        string arg = buffer.substr(pos, argLen);
        tokens.push_back(arg);
        pos += argLen + 2; // Skip argument and trailing CRLF
    }
    consumed = pos;
    return true;
}

//------------------------------
// Simple Thread Pool
//------------------------------
class ThreadPool
{
private:
    vector<thread> workers;
    queue<function<void()>> tasks;
    mutex queue_mutex;
    condition_variable condition;
    bool stop;

public:
    ThreadPool(size_t threads) : stop(false)
    {
        for (size_t i = 0; i < threads; ++i)
        {
            workers.emplace_back(
                [this]
                {
                    for (;;)
                    {
                        function<void()> task;
                        {
                            unique_lock<mutex> lock(this->queue_mutex);
                            this->condition.wait(lock, [this]
                                                 { return this->stop || !this->tasks.empty(); });
                            if (this->stop && this->tasks.empty())
                                return;
                            task = move(this->tasks.front());
                            this->tasks.pop();
                        }
                        task();
                    }
                });
        }
    }
    void enqueue(function<void()> task)
    {
        {
            unique_lock<mutex> lock(queue_mutex);
            tasks.push(move(task));
        }
        condition.notify_one();
    }
    ~ThreadPool()
    {
        {
            unique_lock<mutex> lock(queue_mutex);
            stop = true;
        }
        condition.notify_all();
        for (thread &worker : workers)
            worker.join();
    }
};

//------------------------------
// BlinkDB Server with Epoll and Thread Pool
//------------------------------
class BlinkDBServer
{
private:
    int port;
    int server_fd;
    StorageEngine storage;
    mutex log_mutex;
    ofstream log_file; // Log file stream
    ThreadPool threadPool;
    // Map for persisting partial command buffers per client fd.
    unordered_map<int, string> connectionBuffers;
    mutex connectionBuffersMutex;

    // Logging helper: logs to both console and file.
    void log(const string &message)
    {
        lock_guard<mutex> lock(log_mutex);
        if (log_file.is_open())
            log_file << "[LOG] " << message << endl;
        // cout << "[LOG] " << message << endl;
    }

    // Set file descriptor to non-blocking mode
    void setNonBlocking(int fd)
    {
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags < 0)
        {
            perror("fcntl(F_GETFL) failed");
            exit(EXIT_FAILURE);
        }
        if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
        {
            perror("fcntl(F_SETFL) failed");
            exit(EXIT_FAILURE);
        }
    }

    // Process a single client fd. This function is enqueued on the thread pool.
    void handleClient(int client_fd)
    {
        string localBuffer;
        {
            lock_guard<mutex> lock(connectionBuffersMutex);
            localBuffer = connectionBuffers[client_fd];
        }
        char temp[1024];
        bool connectionClosed = false;
        // Read as much data as possible
        while (true)
        {
            memset(temp, 0, sizeof(temp));
            ssize_t bytes_read = read(client_fd, temp, sizeof(temp));
            if (bytes_read < 0)
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    // No more data to read right now
                    break;
                }
                else
                {
                    log("Read error on client fd " + to_string(client_fd) + ", closing connection.");
                    connectionClosed = true;
                    break;
                }
            }
            else if (bytes_read == 0)
            {
                // Client disconnected
                log("Client fd " + to_string(client_fd) + " disconnected.");
                connectionClosed = true;
                break;
            }
            localBuffer.append(temp, bytes_read);
        }
        if (connectionClosed)
        {
            close(client_fd);
            lock_guard<mutex> lock(connectionBuffersMutex);
            connectionBuffers.erase(client_fd);
            return;
        }

        // Process complete commands in the buffer
        while (true)
        {
            vector<string> tokens;
            size_t consumed = 0;
            if (!tryParseCommand(localBuffer, tokens, consumed))
                break; // No complete command available yet

            // Process command in a case-sensitive manner (upper-case expected)
            string command = tokens[0];
            string response;
            if (command == "SET" && tokens.size() == 3)
            {
                storage.set(tokens[1], tokens[2]);
                response = RESP::encodeSimpleString("OK");
                log("Set: " + tokens[1] + " -> " + tokens[2]);
            }
            else if (command == "GET" && tokens.size() == 2)
            {
                string value = storage.get(tokens[1]);
                response = RESP::encodeBulkString(value);
                log("Get: " + tokens[1] + " -> " + value);
            }
            else if (command == "DEL" && tokens.size() == 2)
            {
                string currentValue = storage.get(tokens[1]);
                int count = (currentValue != "null") ? 1 : 0;
                storage.del(tokens[1]);
                if (count == 1)
                    log("Deleted: " + tokens[1]);
                else
                    log("Delete failed: " + tokens[1] + " does not exist.");
                response = RESP::encodeInteger(count);
            }
            else if (command == "CONFIG" && tokens.size() >= 3 && tokens[1] == "GET")
            {
                // Return an empty array for CONFIG GET
                response = "*0\r\n";
                log("CONFIG GET executed");
            }
            else if (command == "COMMAND" && tokens.size() >= 2 && tokens[1] == "DOCS")
            {
                response = "*0\r\n"; // Return an empty array
            }
            else
            {
                response = RESP::encodeError("Unknown command");
                log("Unknown command: " + command);
            }

            // Write the response back to the client.
            ssize_t bytes_written = write(client_fd, response.c_str(), response.size());
            if (bytes_written <= 0)
            {
                log("Write error on client fd " + to_string(client_fd) + ", closing connection.");
                close(client_fd);
                lock_guard<mutex> lock(connectionBuffersMutex);
                connectionBuffers.erase(client_fd);
                return;
            }
            // Remove processed command from the buffer
            localBuffer.erase(0, consumed);
        }
        // Save any remaining (partial) data back into the connection state
        {
            lock_guard<mutex> lock(connectionBuffersMutex);
            connectionBuffers[client_fd] = localBuffer;
        }
    }

public:
    // The constructor accepts the port and a thread pool size.
    BlinkDBServer(int port, size_t threadPoolSize)
        : port(port), server_fd(-1), threadPool(threadPoolSize)
    {
        log_file.open("log_file.log", ios::out | ios::app);
        if (!log_file.is_open())
        {
            cerr << "Failed to open log file. Logging to console only." << endl;
        }
    }
    ~BlinkDBServer()
    {
        if (log_file.is_open())
            log_file.close();
    }

    // Start the TCP server.
    void start()
    {
        // Create server socket
        server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd < 0)
        {
            perror("Socket creation failed");
            exit(EXIT_FAILURE);
        }

        // Set socket options
        int opt = 1;
        if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
        {
            perror("setsockopt failed");
            close(server_fd);
            exit(EXIT_FAILURE);
        }

        // Bind the server socket
        struct sockaddr_in address;
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(port);
        if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
        {
            perror("Bind failed");
            close(server_fd);
            exit(EXIT_FAILURE);
        }

        // Listen on the socket (backlog increased for high concurrency)
        if (listen(server_fd, 1000) < 0)
        {
            perror("Listen failed");
            close(server_fd);
            exit(EXIT_FAILURE);
        }

        log("Server started on port " + to_string(port));

        // Create epoll instance
        int epoll_fd = epoll_create1(0);
        if (epoll_fd < 0)
        {
            perror("Epoll creation failed");
            close(server_fd);
            exit(EXIT_FAILURE);
        }

        struct epoll_event event;
        event.events = EPOLLIN;
        event.data.fd = server_fd;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event) < 0)
        {
            perror("Epoll_ctl failed");
            close(server_fd);
            close(epoll_fd);
            exit(EXIT_FAILURE);
        }

        // Main event loop
        const int MAX_EVENTS = 1000;
        struct epoll_event events[MAX_EVENTS];
        while (true)
        {
            int num_events = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
            if (num_events < 0)
            {
                perror("Epoll_wait failed");
                break;
            }

            for (int i = 0; i < num_events; i++)
            {
                if (events[i].data.fd == server_fd)
                {
                    // Accept new connection
                    struct sockaddr_in client_addr;
                    socklen_t client_len = sizeof(client_addr);
                    int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
                    if (client_fd < 0)
                    {
                        perror("Accept failed");
                        continue;
                    }

                    setNonBlocking(client_fd);

                    // Add the new client to epoll in edge-triggered mode
                    event.events = EPOLLIN | EPOLLET;
                    event.data.fd = client_fd;
                    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event) < 0)
                    {
                        perror("Epoll_ctl for client failed");
                        close(client_fd);
                        continue;
                    }

                    // Initialize the connection buffer for this client
                    {
                        lock_guard<mutex> lock(connectionBuffersMutex);
                        connectionBuffers[client_fd] = "";
                    }

                    log("New client connected: fd " + to_string(client_fd));
                }
                else
                {
                    // For each client event, dispatch the request to the thread pool.
                    int client_fd = events[i].data.fd;
                    threadPool.enqueue([this, client_fd]()
                                       { this->handleClient(client_fd); });
                }
            }
        }

        close(server_fd);
        close(epoll_fd);
    }
};

//------------------------------
// Main Entry Point
//------------------------------
int main()
{
    // Determine an appropriate thread pool size based on hardware concurrency.
    size_t threadPoolSize = thread::hardware_concurrency();
    if (threadPoolSize == 0)
        threadPoolSize = 4; // Fallback if undetectable
    BlinkDBServer server(9001, threadPoolSize);
    server.start();
    return 0;
}
