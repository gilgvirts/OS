/*
 * blockchain.cpp
 *
 *  Created on: Apr 29, 2015
 *      Author: moshemandel
 */
#include <errno.h>
#include <pthread.h>
#include <list>
#include <vector>
#include <queue>
#include <unistd.h>
#include <map>
#include "block.h"
#include "hash.h"
#include <iostream>
#include <cstdlib>

#define NON_EXIST	-2
#define FAILURE		-1
#define SUCCESS		0

enum closeStatus {OPEN,CLOSING,CLOSED};
closeStatus gStatus = CLOSED;

//a list holding blocks that their hash was not yet computed
pthread_mutex_t gBlocksQueueLock;
std::list<Block*> gBlocksQueue;

//a map holding all blocks in chain
pthread_mutex_t gBlocksLock;
std::map<int,Block*> gBlocks;

//longest chain size
int gLongestChainSize;

//number of blocks were added since chain was initialized
pthread_mutex_t gblocksNumLock;
int gblocksNum;

//priority list holding numbers of blocks that were pruned
pthread_mutex_t gAvailNumLock;
std::priority_queue<int, std::vector<int>, std::greater<int> > gAvailNum;

//a map of the leaves of the current longest chains
std::map<int, Block*> gLongestLeaves;
pthread_mutex_t gLongestLeavesLock;

//conditional variable for daemon thread
pthread_mutex_t gDaemonLock;
pthread_cond_t gDaemon_cv;


pthread_t gDaemon , gClosingTh;

//return pointer to a leaf of one of the current longest chains
Block* getRandomLongestLeaf()
{
	pthread_mutex_lock(&gLongestLeavesLock);
	std::map<int, Block*>::iterator begin = gLongestLeaves.begin();
	std::map<int, Block*>::iterator end = gLongestLeaves.end();
	unsigned int n = std::distance(begin, end);
	unsigned int divisor = (RAND_MAX) / n;

	unsigned int k;
	do { k = std::rand() / divisor; } while (k >= n);

	std::advance(begin, k);

	pthread_mutex_unlock(&gLongestLeavesLock);
	return begin->second;
}

//a daemon function which computes hash of blocks that were added to chain
void* daemonFunc(void*)
{
    while (true)
    {
    	pthread_mutex_lock(&gBlocksQueueLock);
    	while(gBlocksQueue.empty() )
    	{
//    		we are closing
    		if (gStatus != OPEN)
			{
    			pthread_mutex_unlock(&gBlocksQueueLock);
				pthread_exit(NULL);
			}
    		pthread_cond_wait(&gDaemon_cv,&gBlocksQueueLock);

    	}

//      there are blocks waiting for hashing
        Block* toCompute = gBlocksQueue.front();
        gBlocksQueue.pop_front();
        pthread_mutex_unlock (&gBlocksQueueLock);
        toCompute->setWasAdded();

        {


            if (gStatus == OPEN)
            {
                if(toCompute->getToLongest())
                {
                	Block* father = getRandomLongestLeaf();
					toCompute->setFather(father);
					toCompute->setFatherNum(father->getNum());
                }
                int nonce = generate_nonce(toCompute->getNum(), toCompute->getFatherNum()) ;
                char* hash = generate_hash(toCompute->getData() ,toCompute->getDataLength() , nonce);
                toCompute->setHash(hash);


//            	maintaining longestLeaves list
                pthread_mutex_lock(&gLongestLeavesLock);
                if (gLongestLeaves.count(toCompute->getFatherNum() ) )
                {
                	gLongestLeaves.erase(toCompute->getFatherNum() );
                }
                if (toCompute->getDepth() == gLongestChainSize)
                {
                	gLongestLeaves[toCompute->getNum()] = toCompute;
                }
                else if(toCompute->getDepth() > gLongestChainSize)
                {
                	gLongestLeaves.clear();
                	gLongestChainSize = toCompute->getDepth();
                	gLongestLeaves[toCompute->getNum()] = toCompute;
                }
                pthread_mutex_unlock(&gLongestLeavesLock);

                pthread_mutex_lock(&gblocksNumLock);
                gblocksNum++;
                pthread_mutex_unlock(&gblocksNumLock);

                toCompute->setWasHashed(true);
            }

            else // we are closing.
            {
                int nonce = generate_nonce(toCompute->getNum(), toCompute->getFatherNum()) ;
                toCompute->setHash(generate_hash(toCompute->getData() ,toCompute->getDataLength() , nonce) );
                std::cout << "hash of block #" << toCompute->getNum() << ": " << toCompute->getHash() << std::endl;
            }

        }

    }


}

int init_blockchain()
{
    if (gStatus == CLOSING || gStatus == OPEN)
    {
        return FAILURE;
    }

    Block* genesis = new Block();
    init_hash_generator();

    pthread_mutex_init(&gBlocksQueueLock, NULL);
    pthread_mutex_init(&gBlocksLock, NULL);
    pthread_mutex_init(&gAvailNumLock, NULL);
    pthread_mutex_init(&gblocksNumLock, NULL);
    pthread_mutex_init(&gLongestLeavesLock, NULL);

    pthread_mutex_init(&gDaemonLock, NULL);
    pthread_cond_init(&gDaemon_cv, NULL);

    gBlocks[0] = genesis;
    gLongestChainSize = 1;

//    maintaining gLongestLeaves
    gLongestLeaves[0] = genesis;


    if(pthread_create(&gDaemon, NULL, daemonFunc, NULL)) //creat it joinable so we can know if it was finished on close
    {
    	return FAILURE;
    }


    gStatus = OPEN;
    return SUCCESS;

}

int add_block(char *data , int length)
{
    if (gStatus == CLOSING)
    {
        return FAILURE;
    }
    Block* newBlock = new Block(data, length , getRandomLongestLeaf() );


    int num;
    if (gAvailNum.empty())
    {
        pthread_mutex_lock (&gBlocksLock); // find the lowst (new) unused place & occupy it
        num = gBlocks.size();
        gBlocks[num] = newBlock;
        pthread_mutex_unlock (&gBlocksLock);

    }
    else
    {
        pthread_mutex_lock (&gAvailNumLock); //get the freed number safly
        num = gAvailNum.top();
        gAvailNum.pop();
        pthread_mutex_unlock (&gAvailNumLock);

        pthread_mutex_lock (&gBlocksLock);//add the new block to the place safly
        gBlocks[num] = newBlock;
        pthread_mutex_unlock (&gBlocksLock);

    }

    newBlock->setNum(num);
    pthread_mutex_lock (&gBlocksQueueLock);
	gBlocksQueue.push_back(newBlock);
	pthread_cond_signal(&gDaemon_cv);
    pthread_mutex_unlock (&gBlocksQueueLock);
    return num;

}

int to_longest(int block_num)
{
    if (gStatus == CLOSED)
    {
        return FAILURE;
    }
    pthread_mutex_lock (&gBlocksLock);
    if (gBlocks.count(block_num) > 0)
    {
        Block* block = gBlocks[block_num];

        if (!block->getWasAdded())
        {//wasnt added. set to longest!
            block->setToLongest();
            pthread_mutex_unlock (&gBlocksLock);
            return SUCCESS;
        }
        pthread_mutex_unlock (&gBlocksLock);
        return 1;//was added

    }
    else //block dosent exist
    {
        pthread_mutex_unlock (&gBlocksLock);
        return NON_EXIST;
    }
    pthread_mutex_unlock (&gBlocksLock);
    return FAILURE;

}


int attach_now(int block_num)
{
    if (gStatus == CLOSING)
    {
        return FAILURE;
    }
    pthread_mutex_lock (&gBlocksQueueLock);
    for (std::list<Block*>::iterator it = gBlocksQueue.begin(); it != gBlocksQueue.end(); ++it)
    {
        if ((*it)->getNum() == block_num) // block found in 'to add' queue (wasnt added yet)
        {
            gBlocksQueue.erase(it);
            gBlocksQueue.push_front(*it);
            pthread_mutex_unlock (&gBlocksQueueLock);
            return SUCCESS;
        }

    }//its not in the queue.
    pthread_mutex_unlock (&gBlocksQueueLock);
    pthread_mutex_lock (&gBlocksLock);
	if (gBlocks.count(block_num) == 0) // check if block exists
	{
        pthread_mutex_unlock (&gBlocksLock);
        return NON_EXIST;

	}
	else //it exist
	{
        if (gBlocks[block_num]->getWasAdded())
        pthread_mutex_unlock (&gBlocksLock);
        return SUCCESS;
	}


    return FAILURE;

}

int was_added(int block_num)
{
    if (gStatus == CLOSED)
    {
        return FAILURE;
    }
    pthread_mutex_lock(&gBlocksLock);
	if (0 == gBlocks.count(block_num) )
	{
		return NON_EXIST;
	}
	int ret = gBlocks[block_num]->getWasHashed();
	pthread_mutex_unlock(&gBlocksLock);
	return ret;
}

int chain_size()
{
    if (gStatus == CLOSED)
    {
        return FAILURE;
    }

    pthread_mutex_lock (&gblocksNumLock);
    int ret = gblocksNum;
    pthread_mutex_unlock (&gblocksNumLock);
    return ret;
}

int prune_chain()
{
    if (gStatus != OPEN)
    {
        return FAILURE;
    }
	Block* tmp = getRandomLongestLeaf();
//	maintaining gLongestLeaves
	pthread_mutex_lock(&gLongestLeavesLock);
	gLongestLeaves.clear();
	gLongestLeaves[tmp->getNum()] = tmp;
	pthread_mutex_unlock(&gLongestLeavesLock);

	while (tmp->getDepth() != 1)
	{
		tmp->setToPrune(false);
		tmp = tmp->getFather();
	}

//	iterate over gBlocksQueue
	pthread_mutex_lock(&gBlocksQueueLock);
	for(std::list<Block*>::iterator it = gBlocksQueue.begin(); it != gBlocksQueue.end();)
	{
		if(!(*it)->getWasHashed() && (*it)->getFather()->toPrune() )
		{
			pthread_mutex_lock(&gBlocksLock);
			gBlocks.erase((*it)->getNum() );
			pthread_mutex_unlock(&gBlocksLock);

			pthread_mutex_lock (&gAvailNumLock); //get the freed number safly
			gAvailNum.push((*it)->getNum() );
			pthread_mutex_unlock (&gAvailNumLock);

			delete (*it);
			it = gBlocksQueue.erase(it);

		}
		else
		{
			++it;
		}
	}
	pthread_mutex_unlock(&gBlocksQueueLock);

    pthread_mutex_lock(&gBlocksLock);
    for (std::map<int,Block*>::iterator it = gBlocks.begin(); it != gBlocks.end();)
    {
    	if(it->second)
    	{

    		if (it->second->toPrune() && it->second->getWasHashed() )
			{

				delete it->second;
				pthread_mutex_lock (&gAvailNumLock); //get the freed number safely
				gAvailNum.push(it->first );
				pthread_mutex_unlock (&gAvailNumLock);

//				in C++11 map.erase returns the next iterator
				it = gBlocks.erase(it);
			}
    		else
    		{
    			it->second->setToPrune(true);
    			++it;
    		}
    	}

    }
    pthread_mutex_unlock(&gBlocksLock);

    return SUCCESS;

}

//a function used by the close chain, that creates a thread that uses this function
void* closeFunc(void*)
{
	std::list<Block*> notAdded;

	for (std::map<int,Block*>::iterator it = gBlocks.begin(); it != gBlocks.end(); ++it)
    {
        if (it->second->getWasHashed())
        {
            delete it->second; //delete add attached blocks
        }
        else
        {
            notAdded.push_back(it->second);
        }
    }
	gBlocks.clear();
	pthread_mutex_lock(&gBlocksQueueLock);
	pthread_cond_signal(&gDaemon_cv); //tell deamon to exit if empty
	pthread_mutex_unlock(&gBlocksQueueLock);
    pthread_join(gDaemon, NULL); // wait for gDaemon to finish hashing
    //Release remaining blocks
    for (std::list<Block*>::iterator it = notAdded.begin(); it != notAdded.end();)
    {

        delete (*it);
        it = notAdded.erase(it);
    }

    notAdded.clear();
    pthread_mutex_lock(&gLongestLeavesLock);
    gLongestLeaves.clear();
    pthread_mutex_unlock(&gLongestLeavesLock);

    pthread_mutex_lock(&gblocksNumLock);
	gblocksNum = 0;
	pthread_mutex_unlock(&gblocksNumLock);
    close_hash_generator();
    pthread_mutex_destroy(&gBlocksQueueLock);
    pthread_mutex_destroy(&gBlocksLock);
    pthread_mutex_destroy(&gAvailNumLock);
    pthread_mutex_destroy(&gblocksNumLock);
	pthread_mutex_destroy(&gLongestLeavesLock);

	pthread_mutex_destroy(&gDaemonLock);
	pthread_cond_destroy(&gDaemon_cv);

    gStatus = CLOSED;
    pthread_exit(NULL);
}

void close_chain()
{
    gStatus = CLOSING;

    if(pthread_create(&gClosingTh, NULL, closeFunc, NULL))
    {
        exit(1);
    }
}

int return_on_close()
{
    if (gStatus == CLOSED)
    {
        return FAILURE;
    }
    int ret = pthread_join(gClosingTh, NULL);
	switch (ret)
	{
	case 0:
        return 1;

	case ESRCH:
        return NON_EXIST;

    default:
        return FAILURE;
    }

}
