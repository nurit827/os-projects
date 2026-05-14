#include "ReduceContext.h"

ReduceContext::ReduceContext() = default;
ReduceContext::~ReduceContext() = default;

void ReduceContext::addOutput(std::shared_ptr<K3> key, std::shared_ptr<V3> value)
{
    std::lock_guard<std::mutex> lock(mtx);
    vec.push_back({key, value});
}

OutputVec& ReduceContext::getVec()
{
    return vec;
}