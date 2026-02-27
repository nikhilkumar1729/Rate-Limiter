#include <iostream>
#include <string>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <chrono>
#include <atomic>
#include <sstream>
#include <csignal>
#include "httplib.h"

using namespace std;

class Logger {
private:
    static mutex logMutex;
public:
    static void log(const string& level, const string& msg) {
        lock_guard<mutex> lock(logMutex);
        cout << "[" << level << "] " << msg << endl;
    }
};
mutex Logger::logMutex;



class PaymentCache {
private:
    unordered_map<string, string> cache;
    mutex mtx;

public:
    bool exists(const string& key) {
        lock_guard<mutex> lock(mtx);
        return cache.find(key) != cache.end();
    }

    void set(const string& key, const string& value) {
        lock_guard<mutex> lock(mtx);
        cache[key] = value;
    }

    bool get(const string& key, string& value) {
        lock_guard<mutex> lock(mtx);
        auto it = cache.find(key);
        if (it != cache.end()) {
            value = it->second;
            return true;
        }
        return false;
    }
};


enum class PaymentStatus {
    PENDING,
    SUCCESS,
    FAILED
};

struct PaymentRecord {
    string id;
    double amount;
    PaymentStatus status;
    int retryCount;
}

class PaymentGateway {
public:
    bool charge(double amount) {
        if (amount > 10000) { 
            return false;
        }
        return true;
    }
};



class PaymentService {
private:
    unordered_map<string, PaymentRecord> database;
    mutex dbMutex;
    PaymentCache& cache;
    PaymentGateway gateway;
    const int maxRetries = 3;

public:
    PaymentService(PaymentCache& c) : cache(c) {}

    string processPayment(const string& id, double amount) {

        lock_guard<mutex> lock(dbMutex);

        
        if (cache.exists(id)) {
            return "Duplicate payment prevented";
        }

        PaymentRecord record {id, amount, PaymentStatus::PENDING, 0};
        database[id] = record;

        bool success = false;

        while (record.retryCount < maxRetries) {
            success = gateway.charge(amount);
            if (success) break;

            record.retryCount++;
            this_thread::sleep_for(chrono::milliseconds(500 * record.retryCount));
        }

        if (success) {
            record.status = PaymentStatus::SUCCESS;
            cache.set(id, "SUCCESS");
            database[id] = record;
            return "Payment Success";
        } else {
            record.status = PaymentStatus::FAILED;
            database[id] = record;
            return "Payment Failed";
        }
    }
};



class ApplicationService {
public:
    string processLargeForm(const string& jsonForm) {
        if (jsonForm.size() > 10000) {
            return "Form too large";
        }
        return "Application submitted successfully";
    }
};



class BackendServer {
private:
    httplib::Server server;
    PaymentCache cache;
    PaymentService paymentService;
    ApplicationService appService;

public:
    BackendServer() : paymentService(cache) {
        setupRoutes();
    }

    void setupRoutes() {

        server.Post("/payment", [this](const httplib::Request& req, httplib::Response& res) {

            if (!req.has_param("id") || !req.has_param("amount")) {
                res.status = 400;
                res.set_content("Missing parameters", "text/plain");
                return;
            }

            string id = req.get_param_value("id");
            double amount = stod(req.get_param_value("amount"));

            string result = paymentService.processPayment(id, amount);
            res.set_content(result, "text/plain");
        });

        server.Post("/application", [this](const httplib::Request& req, httplib::Response& res) {

            string result = appService.processLargeForm(req.body);
            res.set_content(result, "text/plain");
        });

        server.Get("/health", [](const httplib::Request&, httplib::Response& res) {
            res.set_content("OK", "text/plain");
        });
    }

    void start(int port) {
        Logger::log("INFO", "Backend started on port " + to_string(port));
        server.listen("0.0.0.0", port);
    }
};



int main() {

    signal(SIGINT, [](int){ exit(0); });

    BackendServer server;
    server.start(8080);

    return 0;
}
