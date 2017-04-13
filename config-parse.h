#include<stdio.h>
#include<stdlib.h>
#include<string.h>

#define MAX_ENTRIES 1024

struct Media {
    char type[256];
    char value[256];
};

struct Host {
    char name[256];
    char value[256];
};

struct Media media[MAX_ENTRIES];
struct Host hosts[MAX_ENTRIES];

int number_of_conf_entries;
int number_of_host_entries;
int number_of_media_entries;

int verbose_flag;

void printConfigOptions();
void parseConfig(char *filename);
