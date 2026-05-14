#ifndef MAP_CONTEXT_H
#define MAP_CONTEXT_H

#include "MapReduceKeys.h"

class MapContext
{
private:
    IntermediateVec vec;
public:
    MapContext();
    ~MapContext();

    void addIntermediate(std::shared_ptr<K2> key, std::shared_ptr<V2> value);

    IntermediateVec& getVec();
};

#endif // MAP_CONTEXT_H