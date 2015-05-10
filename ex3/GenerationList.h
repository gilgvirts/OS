#ifndef GENERATIONSLIST_H
#define GENERATIONSLIST_H

#include "Block.h"
#include <pthread.h>
#include <list>


class GenerationList
{
    public:
        GenerationList();
        virtual ~GenerationList();

        void tryAddBlock(Block*);
        Block* getRandBlock();
        void incrCount();
        int getCount();

    protected:
    private:
        std::vector<Block*> _generation;
        pthread_mutex_t _generationLock;
        int _genCount;

};

#endif // GENERATIONSLIST_H
