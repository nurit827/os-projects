#include "MapContext.h"

MapContext::MapContext() = default;

MapContext::~MapContext() = default;

void MapContext::addIntermediate(std::shared_ptr<K2> key, std::shared_ptr<V2> value)
{
    // TODO: implement this function
    vec.push_back({key,value});

}

IntermediateVec& MapContext::getVec()
{
    return vec;
}
