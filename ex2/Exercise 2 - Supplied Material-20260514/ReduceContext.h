#ifndef REDUCE_CONTEXT_H
#define REDUCE_CONTEXT_H

#include <mutex>
#include "MapReduceKeys.h"

class ReduceContext
{
private:
    OutputVec vec;
    std::mutex mtx;
public:
    ReduceContext();
    ~ReduceContext();
    void addOutput(std::shared_ptr<K3> key, std::shared_ptr<V3> value);

    OutputVec& getVec();
};

#endif // REDUCE_CONTEXT_H