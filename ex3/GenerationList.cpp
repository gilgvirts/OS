#include "GenerationsList.h"

GenerationList::GenerationList()
{
    //ctor
    pthread_mutex_init(&_generationLock, NULL);
}

GenerationsList::~GenerationList()
{
    //dtor
    pthread_mutex_destroy(&_generationLock);
}

void GenerationList::tryAddBlock(Block* toAdd)
{
    pthread_mutex_lock (&_generationLock);
    Block
    _generation.push_back(Block*);
}

Block* GenerationList::getRandBlock()
{
    std::random_shuffle(_generation.begin(), _generation.end());
    return _generation.front();
}

void GenerationList::incrCount()
{

}
int GenerationList::getCount()
{

}
