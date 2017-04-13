//
// Created by benjamin on 8/04/17.
//

#ifndef SERVER_QUEUE_H
#define SERVER_QUEUE_H

struct node {
    int client;
    struct node *next;
};

struct queue{
    struct node *head;
    int size;
};

struct queue newQueue();
struct node popQueue(struct queue* q);

struct queue newQueue();
void pushQueue(struct queue* q, int client);
struct node popQueue(struct queue* q);


#endif //SERVER_QUEUE_H
