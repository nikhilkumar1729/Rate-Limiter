#include <iostream>
#include <string>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <csignal>
#include <sstream>
#include "httplib.h"

using namespace std;

class Logger {
public:
    static void info(const string& msg) {
        cout << "[INFO] " << msg << endl;
    }

    static void error(const string& msg) {
        cerr << "[ERROR] " << msg << endl;
    }
};


class Cache {
private:
    unordered_map<string, string> store;
    mutex mtx;

public:
    void set(const string& key, const string& value) {
        lock_guard<mutex> lock(mtx);
        store[key] = value;
    }

    bool get(const string& key, string& value) {
        lock_guard<mutex> lock(mtx);
        if (store.find(key) != store.end()) {
            value = store[key];
            return true;
        }
        return false;
    }
};


class RateLimiter {
private:
    unordered_map<string, int> requestCount;
    mutex mtx;
    const int limit = 5;

public:
    bool allow(const string& clientIp) {
        lock_guard<mutex> lock(mtx);
        requestCount[clientIp]++;
        return requestCount[clientIp] <= limit;
    }

    void reset() {
        lock_guard<mutex> lock(mtx);
        requestCount.clear();
    }
};



class UserService {
private:
    Cache& cache;

public:
    UserService(Cache& c) : cache(c) {}

    string getUser(const string& id) {
        string cached;
        if (cache.get(id, cached)) {
            return cached;
        }

        
        string result = "{\"id\": \"" + id + "\", \"name\": \"User_" + id + "\"}";
        cache.set(id, result);
        return result;
    }
};



class BackendServer {
private:
    httplib::Server server;
    Cache cache;
    RateLimiter limiter;
    UserService userService;
    atomic<bool> running;

public:
    BackendServer() : userService(cache), running(true) {
        setupRoutes();
        startLimiterResetThread();
    }

    void setupRoutes() {

        server.Get("/health", [](const httplib::Request&, httplib::Response& res) {
            res.set_content("OK", "text/plain");
        });

        server.Get("/user", [this](const httplib::Request& req, httplib::Response& res) {

            string clientIp = req.remote_addr;

            if (!limiter.allow(clientIp)) {
                res.status = 429;
                res.set_content("Rate limit exceeded", "text/plain");
                return;
            }

            if (!req.has_param("id")) {
                res.status = 400;
                res.set_content("Missing id parameter", "text/plain");
                return;
            }

            string id = req.get_param_value("id");
            string userJson = userService.getUser(id);

            res.set_content(userJson, "application/json");
        });

        server.Post("/echo", [](const httplib::Request& req, httplib::Response& res) {
            res.set_content(req.body, "application/json");
        });
    }

    void startLimiterResetThread() {
        thread([this]() {
            while (running) {
                this_thread::sleep_for(chrono::seconds(60));
                limiter.reset();
                Logger::info("Rate limiter reset");
            }
        }).detach();
    }

    void start(int port) {
        Logger::info("Starting backend on port " + to_string(port));
        server.listen("0.0.0.0", port);
    }

    void stop() {
        running = false;
        server.stop();
    }
};


BackendServer* globalServer = nullptr;

void signalHandler(int signal) {
    Logger::info("Shutdown signal received");
    if (globalServer) {
        globalServer->stop();
    }
    exit(0);
}


int main() {
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    BackendServer server;
    globalServer = &server;

    server.start(8080);

    return 0;
}
