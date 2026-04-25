#include <queue>
#include <vector>

class IdAllocator {
    int counter = 0;
    std::priority_queue<int, std::vector<int>,std::greater<int>> deleted_threads;
    public:
    int next_id() {
        if (deleted_threads.empty()) {
            counter++;
            return counter;
        }
        int top = deleted_threads.top();
        deleted_threads.pop();
        return top;
    }

    void delete_id(int id) {
        deleted_threads.push(id);
    }
};