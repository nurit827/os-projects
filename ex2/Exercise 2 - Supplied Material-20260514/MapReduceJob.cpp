#include "MapReduceJob.h"

/*
===============================================
Implement:
===============================================
*/

MapReduceJob::MapReduceJob(const MapReduceClient &client, const InputVec &inputVec, int multiThreadLevel)
    : client (client)
    , inputVec (inputVec)
    , multiThreadLevel (multiThreadLevel)
{
    // TODO: implement this constructor  
}

MapReduceState MapReduceJob::getState(void) const
{
    // TODO: implement this function
}

void MapReduceJob::wait(void)
{
    // TODO: implement this function
}

OutputVec MapReduceJob::getOutput(void)
{
    // TODO: implement this function
}

bool MapReduceJob::isDone(void) const
{
    // TODO: implement this function
}

MapReduceJob::~MapReduceJob()
{
    // TODO: implement this destructor
}
