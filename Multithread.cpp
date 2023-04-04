#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <chrono>
#include <random>
#include <vector>
#include <signal.h>

std::mutex mtx;
std::condition_variable cv;

bool stopRequested = false;
std::vector<std::thread> threads;

class Request {
public:
    int group_id;
    int priority;
    int firstNum;
    int secondNum;

    bool operator<(const Request& other) const {
        return priority < other.priority;
    }

    Request (int group_id, int priority, int fNum, int sNum)
        : group_id(group_id), priority(priority), firstNum(fNum), secondNum(sNum) {}
};

class Queue {
    private:
        std::vector<std::priority_queue<Request>> requests_;
        std::mutex mtx_;
        std::condition_variable cv_;
        int max_size_;

    public:
        Queue(int max_size) : max_size_(max_size) {
            for (int i = 0; i < max_size; i++) {
                std::priority_queue<Request> newGroup;
                requests_.push_back(newGroup);
            }
        }

        int size() {
            int sz = 0;
            for (int i = 0; i < requests_.size(); i++) {
                sz += requests_[i].size();
            }

            return sz;
        }

        bool is_full(int group) {
            return requests_[group].size() == max_size_;
        }

        bool is_full_max(int group) {
            int sz = 0;
            for (int i = 0; i < requests_.size(); i++) {
                sz += requests_[i].size();
            }

            return sz == max_size_;
        }

        bool is_empty(int group) {
            return requests_[group].empty();
        }

        void push(Request req, int group) {
            if (stopRequested) {
                return;
            }

            std::unique_lock<std::mutex> lock(mtx_);
            std::cout << "-> In queue now: " << size() << std::endl;

            while (is_full_max(group)) {
                cv_.wait(lock);
            }

            requests_[group].push(req);

            lock.unlock();
            cv.notify_all();
            cv_.notify_all();
        }

        Request pop(int group) {
            std::unique_lock<std::mutex> lock(mtx_);

            while (is_empty(group)) {
                cv_.wait(lock);
            }

            Request req = requests_[group].top();
            requests_[group].pop();

            lock.unlock();
            cv.notify_all();
            cv_.notify_all();

            return req;
        }
};

class Device {
    private:
        int id_;
        int group_id_;
        bool busy_;
        Request current_request_;
        int remaining_time_;

        Queue* queue_;
        std::mutex* mtx_;
        std::condition_variable* cv_;

        std::random_device rd_;
        std::mt19937 gen_;
        std::uniform_int_distribution<> dis_;

    public:
        Device(int id, int group_id, Queue* queue, std::mutex* mtx, std::condition_variable* cv)
            : id_(id), group_id_(group_id), busy_(false), remaining_time_(0),
            queue_(queue), mtx_(mtx), cv_(cv), gen_(rd_()), dis_(1000, 10000),
            current_request_(Request(-1, -1, -1, -1)) {};

        void work() {
            while (!stopRequested) {
                std::unique_lock<std::mutex> lock(*mtx_);
                while (queue_->is_empty(group_id_)) {
                    std::cout << "Device " << id_ << " from group " << group_id_ << " is waiting...\n";
                    cv_->wait(lock);
                    if (stopRequested) {
                        return;
                    }
                }
                Request req = queue_->pop(group_id_);
                int result = 0;
                busy_ = true;
                current_request_ = req;
                remaining_time_ = dis_(gen_);


                std::cout << "Device " << id_ << " from group " << group_id_ << " started processing request ";
                std::cout << req.group_id << "-" << req.priority << " (remaining time: " << remaining_time_;
                std::cout << " ms | solve: " << req.firstNum << " + " << req.secondNum << ")" << std::endl;
                
                lock.unlock();
                
                result = req.firstNum + req.secondNum;
                std::this_thread::sleep_for(std::chrono::milliseconds(remaining_time_));

                lock.lock();

                std::cout << "Device " << id_ << " from group " << group_id_ << " finished processing request ";
                std::cout << current_request_.group_id << "-" << current_request_.priority << " | (" << req.firstNum;
                std::cout << " + " << req.secondNum << " = " << result << ")" << std::endl;

                busy_ = false;
                current_request_ = Request(-1, -1, -1, -1);
                cv_->notify_all();
                lock.unlock();
            }
        }
};

void generate_requests(Queue& queue, int groups) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> group_dis(1, groups);
    std::uniform_int_distribution<> priority_dis(1, 3);
    std::uniform_int_distribution<> sleep_dis(100, 500);
    std::uniform_int_distribution<> num_dis(0, 100);

    while (!stopRequested) {
        int group_id = group_dis(gen);
        int priority = priority_dis(gen);
        int firstNum = num_dis(gen);
        int secondNum = num_dis(gen);

        Request req(group_id, priority, firstNum, secondNum);
        if (!queue.is_full(group_id)) {
            queue.push(req, group_id);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_dis(gen)));
    }
}

void create_devices(int num_devices, int groups, Queue& queue, std::mutex& mtx, std::condition_variable& cv) {
    
    for (int j = 0; j < groups; j++) {
        for (int i = 0; i < num_devices; ++i) {
            threads.emplace_back([&queue, &mtx, &cv, i, j]() {
                Device dev(i + 1, j + 1, &queue, &mtx, &cv);
                dev.work();
                });
        }
    }

    for (auto& t : threads) {
        t.join();
    }
}

void signal_handler(int signal) {
    std::cout << "!!! | Request to shutdown logged. Waiting for threads to finish... | !!!" << std::endl;

    stopRequested = true;
    cv.notify_all();
}

int main() {
    int size;
    int deviceNum;
    int groups;

    signal(SIGINT, signal_handler);

    std::cout << "Enter size of queue: ";
    std::cin >> size;
    std::cout << std::endl;
    
    std::cout << "Enter devices in group: ";
    std::cin >> deviceNum;
    std::cout << std::endl;

    std::cout << "Enter number of groups: ";
    std::cin >> groups;
    std::cout << std::endl;
    
    Queue queue(size);

    std::thread generator(generate_requests, std::ref(queue), groups);
    std::thread startDevices(create_devices, deviceNum, groups, std::ref(queue), std::ref(mtx), std::ref(cv));

    generator.detach();
    startDevices.join();

    std::cout << "All threads finished. Exiting...\n" << std::endl;

    return 0;
}
