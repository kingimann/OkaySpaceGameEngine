// <thread> and <condition_variable> ([thread.threads], [thread.condition]): okay-cc runs a thread's
// function when it is joined or waited for (one thread of execution); the results printed after
// join are the same as with real threads.
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>
struct Worker {
    std::mutex m;
    std::condition_variable cv;
    bool ready = false;
    int value = 0;
    std::thread t;
    void Start() {
        t = std::thread([this] {
            std::lock_guard<std::mutex> lk(m);
            value = 42;
            ready = true;
            cv.notify_one();
        });
    }
};
static void addTo(int *out, int n) { *out += n; }
int main() {
    std::vector<int> parts(4, 0);
    std::vector<std::thread> ts;
    for (int i = 0; i < 4; ++i) ts.emplace_back([&parts, i] { parts[i] = i * i; });
    for (auto &t : ts) t.join();
    std::printf("%d %d %d %d\n", parts[0], parts[1], parts[2], parts[3]);
    Worker w;
    w.Start();
    {
        std::unique_lock<std::mutex> lk(w.m);
        w.cv.wait(lk, [&] { return w.ready; });
        std::printf("value %d\n", w.value);
    }
    w.t.join();
    int total = 1;
    std::thread a(addTo, &total, 5);
    a.join();
    std::printf("%d %d\n", total, (int)a.joinable());
    std::thread::id me = std::this_thread::get_id();
    std::thread b([&] { std::printf("other %d\n", (int)(std::this_thread::get_id() != me)); });
    b.join();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    std::printf("%d\n", (int)(std::thread::hardware_concurrency() >= 1));
}
