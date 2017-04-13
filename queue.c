//
// Created by benjamin on 8/04/17.
//
#include "queue.h"

void cleanQueue(){

}

void pushQueue(struct queue* q, int client){

    struct node *n = malloc(sizeof(struct node));
    n->client = client;

    if(q->head){
        struct node *currrentNode = q->head;
        while(currrentNode->next){
            currrentNode = currrentNode->next;
        }
        currrentNode->next = n;
        ++q->size;
    }else{
        q->head = n;
        ++q->size;
    }

}

struct node popQueue(struct queue* q){

    struct node headNodeCopy;
    struct node* headNodeAddress;
    if(q->size != 0){
        headNodeCopy = *q->head;
        headNodeAddress = q->head;
        --q->size;
        if(q->size != 0){
            q->head = q->head->next;
            printf("  New head has the client: %i\n", q->head->client);
        }else{
            q->head = 0;
        }

    }
    free(headNodeAddress);
    return headNodeCopy;
}

struct queue newQueue(){
    struct queue q;
    q.size = 0;
    q.head = 0;

    return q;
}

void freeQueue(struct queue* q){
    while(q->size != 0){
        popQueue(q);
    }
}


